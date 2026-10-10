# GatewayV2 — Kế hoạch lớn hoàn thiện giao tiếp thương mại

**Phạm vi:** Dashboard/API, MQTT, RS485/Modbus và communication runtime.  
**Ngoài phạm vi:** USB Config vẫn vô hiệu hóa; không thay đổi vendor library ST67; không đổi kiến trúc WiFi/LTE/DNS hiện tại trừ các notify cần thiết khi apply config.

## 1. Mục tiêu sản phẩm

Gateway phải có một nguồn sự thật duy nhất cho cấu hình, quyền sở hữu phần cứng và trạng thái runtime. Dashboard chỉ gửi desired configuration và đọc trạng thái đã xác nhận. Firmware phải chịu được config POST, route failover, MQTT reconnect, Modbus polling và TCP client disconnect đồng thời mà không reset, treo hoặc rò heap.

### Tiêu chí tài nguyên

- Minimum-ever-free heap mục tiêu: `>= 96 KB`; ngưỡng chặn release: `>= 64 KB`.
- Không cấp phát toàn bộ payload/firmware/request trên heap.
- Không để HTTP request chặn `tcpip_thread`, WiFi, LTE, MQTT hoặc RS485.
- Mọi queue/buffer runtime quan trọng có giới hạn cố định và có counter overflow.

## 2. Mô hình domain thống nhất

```text
Dashboard
   │ REST: desired config + status
   ▼
Config Core ── generation/apply state ── Runtime Apply Coordinator
                                           │
                  ┌────────────────────────┼─────────────────────┐
                  ▼                        ▼                     ▼
           RS485 Port Manager       MQTT Connection Manager   Network manager
                  │                        │
          Modbus/TCP/Serial          Publisher Engine
                  │                        │
                  └──────────► Telemetry Store ◄───────────────┘
```

Các thuật ngữ bắt buộc:

- `desired configuration` và `runtime configuration` là hai trạng thái khác nhau.
- `configured_mode`, `owner`, `runtime_state` không được gộp vào một field.
- `saved`, `applied`, `apply_state` phải được trả riêng trong API.

## 3. Nguyên tắc thiết kế module và seam

Module trung tâm là `RS485 Port Manager`: interface nhỏ, implementation chịu trách nhiệm state machine, ownership, drain, UART/DMA/DE và counters. Các task Modbus, Serial TCP và Modbus TCP chỉ gọi interface này; không tự cấu hình UART hoặc GPIO DE.

Interface dự kiến:

```c
rs485_port_init(void);
rs485_port_acquire(uint8_t port, rs485_owner_t owner);
rs485_port_release(uint8_t port, rs485_owner_t owner);
rs485_port_apply_config(uint8_t port, const rs485_runtime_config_t *cfg);
rs485_port_get_status(uint8_t port, rs485_port_status_t *out);
rs485_port_reconfigure(uint8_t port, const rs485_runtime_config_t *cfg);
rs485_port_wait_tx_idle(uint8_t port, TickType_t timeout);
```

Invariant của interface:

- acquire khi đã có owner trả lỗi rõ ràng, không cướp quyền.
- release sai owner bị từ chối.
- trước khi reconfigure phải stop service, drain queue, chờ TX idle và đưa DE về RX.
- Port Manager chỉ quản lý UART/DMA/DE/owner/hardware state/stats; không tự stop TCP server, Modbus poller hoặc transaction service.
- API không được gọi thao tác dài từ HTTP callback, ISR hoặc vendor callback.
- `RS485_OWNER_NONE` độc lập với `runtime_state`; `RECONFIGURING` hoặc `ERROR` có thể có owner `NONE`.
- Owner thuộc service, không thuộc TCP client/session.

Runtime service lifecycle do Service Manager sở hữu:

```text
Runtime Apply Coordinator → Service Manager: stop/drain/start service
Service Manager → RS485 Port Manager: wait idle/release/reconfigure/acquire
```

## 4. Lộ trình triển khai theo phase

### Phase 0 — Đóng băng contract và baseline

**Mục tiêu:** chốt hành vi trước khi sửa implementation.

- Đóng băng schema/API version, `generation`, `base_generation`, `apply_state`.
- Ghi baseline heap/stack/CPU và log boot với WiFi/LTE/MQTT/HTTP/Modbus.
- Xác nhận capabilities phản ánh đúng tính năng thật; feature chưa có trả `422 unsupported_feature`.
- Đưa `GET /api/capabilities` lên endpoint nền tảng, response có `features` và `limits`:
  `rs485_ports`, `max_modbus_devices`, `max_modbus_points`, `max_mqtt_publishers`, `config_body_bytes`.
- Chốt P0 config body là `16 KiB`; capability phải trả `config_body_bytes: 16384`.
- Chốt endpoint:
  - `GET /api/status` — summary.
  - `GET /api/rs485/status` — chi tiết từng port.
  - `GET /api/mqtt/status` — connection/publisher.
  - `GET /api/capabilities` — feature và limits.
- Không thay đổi runtime trong phase này.

**Gate:** API contract và baseline được ghi lại; build Release pass; USB vẫn tắt; capabilities phản ánh đúng runtime.

### Phase 1 — Config Core transactional

**Mục tiêu:** desired config an toàn, không làm hỏng active config.

Luồng bắt buộc:

```text
body limit → parse → staging → validate → capability check
→ serialize temp → verify → atomic rename → commit generation
→ enqueue apply request
```

- `base_generation` sai trả `409 conflict`.
- JSON/field sai trả `400` với `path`, `code`, `message`.
- Mode chưa hỗ trợ trả `422`.
- Password GET chỉ trả `password_set`; password thiếu trong POST thì giữ cũ.
- Không notify module nếu commit thất bại.
- Apply request dùng queue/task, không chạy trong HTTP callback.
- cJSON DOM chỉ tồn tại trong transaction; không chuyển cJSON giữa các task.
- Runtime Apply Coordinator chỉ phát `CONFIG_APPLY(generation)` hoặc các runtime snapshot nhỏ theo module; không copy toàn bộ `system_config_t` cho mọi module.
- Persistent commit phải power-loss-safe; `atomic rename` chỉ là một implementation có thể thay thế bằng slot A/B + generation + CRC.

**Gate:** config cũ đọc được; lỗi parse/validate/flash đều rollback; mất điện khi ghi không mất file cũ; heap min đạt ngưỡng.

### Phase 2 — Runtime Apply Coordinator

**Mục tiêu:** tách “đã lưu” khỏi “đã chạy”.

State:

```text
pending → applying → applied
                  └→ error
```

- Mỗi module nhận một apply request có generation.
- Coordinator chờ kết quả từng module và lưu `last_error`.
- Config mới không làm mất config đang chạy nếu apply thất bại.
- Khi desired generation `42` lưu thành công nhưng apply lỗi trong lúc runtime đang chạy generation `41`:
  `generation=42`, `applied_generation=41`, `saved=true`, `apply_state=error`.
- Không tự rollback persistent config về generation 41; dashboard có thể Retry Apply hoặc người dùng tạo config mới.
- API trả `saved`, `generation`, `apply_state`, `applied_generation`, `reboot_required`, `last_error` gồm module/port/code.

**Gate:** đổi MQTT không làm dừng network; đổi RS485 không làm HTTP/WiFi reset; apply lỗi có thể retry.

### Phase 3 — RS485 Port Manager

**Mục tiêu:** một owner duy nhất cho mỗi cổng.

State machine:

```text
DISABLED → STOPPED → STARTING → RUNNING
                         │          │
                         └──────► ERROR
RUNNING → STOPPING → RECONFIGURING → STARTING
```

Chuyển mode:

```text
validate → stop service → drain/cancel → wait UART idle
→ DE=RX → release owner → apply UART/DMA → acquire owner mới
→ start → verify → RUNNING
```

`owner` dùng enum độc lập, tối thiểu: `NONE`, `SERIAL_TCP_SERVER`, `SERIAL_TCP_CLIENT`, `MODBUS_MASTER`, `MODBUS_GATEWAY`.

Counters tối thiểu: RX/TX bytes, RX/TX frames, queue depth/max, overflow, timeout, CRC/UART error, last activity, last error.

**Gate:** acquire cạnh tranh bị từ chối; mode change lặp 100 lần không leak/reset; DE luôn an toàn; không có hai task dùng cùng UART/DMA.

### Phase 4 — Vertical slice Port 0 và di chuyển service

**Mục tiêu:** kiểm chứng seam trước bằng `Port 0 → Serial TCP Server → runtime status → mode change → stress 100 chu kỳ`, sau đó mới di chuyển toàn bộ `MB_P0..MB_P2` và Modbus TCP.

- Modbus master chỉ chạy khi owner là `MODBUS_MASTER`.
- Serial TCP raw bridge chỉ chạy khi owner là `SERIAL_TCP_SERVER`.
- Modbus TCP server không tự suy đoán port; lấy runtime status từ manager.
- Không tạo task cho mode disabled/unsupported.
- Khi TCP client disconnect: đóng session, flush/cancel buffer của client, cập nhật `client_count`; giữ owner `SERIAL_TCP_SERVER` khi service vẫn RUNNING.
- Chỉ release owner khi disable port, đổi mode hoặc teardown service/error.

**Gate:** boot regression; mỗi port có owner/state rõ; client TCP thứ hai bị từ chối; queue overflow không làm reset.

### Phase 5 — Runtime status và API detail

- `/api/status` chỉ trả summary nhỏ, không tạo bản sao heap lớn.
- `/api/rs485/status` trả từng port: configured mode, owner, runtime state, counters, last error.
- `/api/mqtt/status` trả connection state, route/generation, DNS/connect failures, publisher counters.
- Timestamp/last activity dùng monotonic time; không phụ thuộc NTP.
- Dashboard chỉ hiển thị `Running` khi runtime state đã xác nhận.

**Gate:** 1–4 HTTP client; không request chồng lấn; response không truncate; heap sau GET ổn định.

### Phase 6 — Telemetry Store và data flow

```text
Modbus Poller → Telemetry Store → MQTT Publisher
```

- Poller cập nhật snapshot theo device/point.
- Publisher đọc snapshot nhất quán; không gọi Modbus hoặc UART lúc publish.
- Snapshot có sequence/generation để tránh payload trộn giữa hai lần đọc.
- Nếu chưa có dữ liệu, payload phải thể hiện `stale`/timestamp, không block publish.
- Resolve data key chuỗi thành `telemetry_id_t` số trong lúc apply; runtime dùng `telemetry_get(id, ...)`, không lookup chuỗi ở mỗi publish.
- Mỗi point có quality: `INVALID`, `GOOD`, `STALE`, `TIMEOUT`, `ERROR`; timeout giữ giá trị cuối nhưng đổi quality.

**Gate:** MQTT publish không ảnh hưởng poll timing; snapshot không race; payload hợp lệ khi device mất kết nối.

### Phase 7 — MQTT Connection Manager và Publisher Engine

Tách hai module:

- Connection Manager: broker, DNS, route generation, reconnect/backoff, keepalive, socket lifecycle.
- Publisher Engine: topic, QoS, retain, interval, strategy, payload builder/template.

Yêu cầu:

- Một broker connection duy nhất.
- Route/DNS đổi thì hủy request cũ và reconnect theo generation mới.
- `enabled=false` dừng publisher và retry.
- Publisher không tự tạo connection hoặc thao tác Modbus.
- P0 chỉ hỗ trợ publish `periodic`; `on_change` để capability riêng và phase sau vì cần chốt epsilon, quality transition và minimum interval.
- Counters: published, dropped, encode errors, reconnects, queue high-water.

**Gate:** WiFi→ETH→LTE failover; MQTT không dùng DNS của route cũ; config MQTT apply không làm crash; periodic publish ổn định; `mqtt_on_change=false`; heap min đạt ngưỡng.

### Phase 8 — Dashboard v16 integration

- Dashboard đọc capabilities trước khi bật control.
- Gửi desired config kèm `base_generation`.
- Hiển thị `saved` và `apply_state` riêng.
- Poll status theo trang; không polling discovery/telemetry khi không cần.
- Hiển thị lỗi `400/401/409/413/422/500` theo field.
- Không lưu password; không hiển thị trạng thái Running giả.

**Gate:** dashboard v16 đọc API; config conflict được xử lý; 401 không gây retry vô hạn. Dashboard cũ chỉ được hỗ trợ qua migration layer trong một migration window có thời hạn; không cam kết hỗ trợ vô thời hạn mọi schema POST cũ.

### Phase 9 — Mở rộng sau P0

Chỉ làm sau khi P0 ổn định:

1. Ba port đầy đủ và giới hạn resource.
2. TCP Client.
3. Modbus RTU Master hoàn thiện.
4. MQTT nhiều publisher trên cùng connection.
5. Modbus TCP↔RTU Gateway.
6. OTA/dashboard workflow nâng cao.

Mỗi feature phải có capability, validation, ownership, counters và test riêng.

## 5. Phân công implementation/test

### AI/Senior implementation

- Thực hiện từng phase, không mở rộng phạm vi.
- Mỗi phase có commit riêng, build Release và log thay đổi RAM/Flash.
- Không sửa vendor library nếu chưa có bằng chứng độc lập.

### AI/Senior test/review

- Đọc contract trước implementation.
- Test ownership, conflict, rollback, disconnect, overflow, failover và heap.
- Báo lỗi theo blocker/critical/major/minor.
- Không sửa implementation trước khi ghi nhận lỗi.

Chỉ chuyển phase khi implementation build pass và test report pass độc lập.

## 6. Ma trận kiểm thử nghiệm thu

### Config/API

- config hợp lệ, JSON lỗi, body >16 KiB, generation conflict, unsupported feature.
- password masking và giữ password khi field thiếu.
- lỗi LittleFS/rename, mất điện khi ghi, hai POST đồng thời.

### RS485

- acquire/release đúng và sai owner.
- hai service cạnh tranh cùng port.
- mode change khi TX đang bận.
- TCP disconnect giữa frame.
- queue đầy, timeout, CRC/UART error.
- rút/cắm/đổi cấu hình tối thiểu 100 chu kỳ.

### MQTT/network

- WiFi/ETH/LTE route chuyển đổi.
- DNS luôn khớp route hiện tại.
- broker lỗi, DNS lỗi, mất socket, backoff.
- config MQTT đổi trong lúc reconnect.

### Reliability/resources

- 1–4 dashboard clients.
- GET status/config và POST config đồng thời MQTT/Modbus.
- chạy tối thiểu 30 phút; không watchdog, hardfault, lwIP assert hoặc heap leak.
- heap min `>=64 KB`, mục tiêu `>=96 KB`.

## 7. Milestones và Definition of Done

Để tránh milestone đầu tiên quá lớn, chia P0:

### P0-A — Foundation

- Config Core transactional.
- Runtime Apply Coordinator.
- RS485 Port Manager.
- Vertical slice Port 0 Serial TCP Server.
- Status API và capabilities/limits.

### P0-B — Data path

- Modbus Master qua Port Manager.
- Telemetry Store với numeric ID/quality.
- MQTT Connection Manager + periodic Publisher Engine.

### P0-C — Product integration

- Dashboard v16.
- migration layer có thời hạn.
- regression WiFi/LTE/ETH/HTTP/MQTT/RS485.

Chỉ chuyển P0-A → P0-B → P0-C khi gate độc lập đạt.

## 8. Definition of Done cho P0

- Config transaction và apply state hoàn chỉnh.
- RS485 Port Manager là owner duy nhất.
- Một Serial TCP Server hoặc mode P0 được xác nhận chạy thực tế.
- API detail và capabilities phản ánh runtime thật.
- MQTT connection/publisher không truy cập trực tiếp Modbus.
- Dashboard v16 hiển thị đúng desired/runtime state.
- WiFi/LTE/ETH/HTTP/MQTT regression pass.
- USB tiếp tục disabled.
- Có build artifact, log board và test report cho từng phase.

## 9. Đo lường và phát hành

- `minimum-ever-free heap` được đo từ boot đến cuối scenario chuẩn, không lấy một log ngẫu nhiên.
- Scenario chuẩn: boot → ETH/WiFi/LTE → HTTP → MQTT connected → 3 RS485 services → 4 dashboard clients → config POST → OTA status polling → 30 phút chạy.
- Nếu allocator hỗ trợ, ghi thêm `largest_free_block` để phát hiện fragmentation.
- 30 phút là phase gate; release candidate phải soak test 8–24 giờ với route failover, MQTT reconnect, Modbus polling, HTTP polling và config reads.

## 10. Quy tắc phát hành

- Mỗi phase một commit có tiêu đề rõ.
- Không commit `build-t01`, file trung gian hoặc log chứa credential.
- Không flash board tự động trong phase build.
- Chỉ release khi tất cả gate và regression pass.
