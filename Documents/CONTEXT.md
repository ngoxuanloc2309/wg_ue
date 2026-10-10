# GatewayV2 domain context

## Canonical terms

- **Desired configuration**: cấu hình đã được người dùng gửi và lưu thành công.
- **Runtime configuration**: cấu hình mà các module đã áp dụng vào phần cứng và task đang chạy.
- **Generation**: số phiên bản tăng đơn điệu của desired configuration sau mỗi lần commit thành công.
- **Apply state**: tiến trình đưa desired configuration vào runtime: `pending`, `applying`, `applied` hoặc `error`.
- **RS485 owner**: đúng một module đang có quyền sử dụng một cổng RS485 tại một thời điểm.
- **Owner service**: service sở hữu phần cứng trong suốt lifecycle của service; không thay đổi chỉ vì một client/session kết nối hoặc ngắt kết nối.
- **OWNER_NONE**: trạng thái không có service nào sở hữu cổng; độc lập với `runtime_state`.
- **Configured mode**: mode được yêu cầu trong desired configuration.
- **Runtime state**: trạng thái thực tế của module sau khi apply, không suy ra chỉ từ cấu hình.
- **Telemetry Store**: snapshot dữ liệu đo gần nhất, do poller cập nhật và publisher chỉ đọc.
- **Telemetry point ID**: định danh số ổn định dùng trong runtime; chuỗi data key chỉ dùng ở config/UI/log và được resolve khi apply.
- **Telemetry quality**: chất lượng điểm đo, tối thiểu `INVALID`, `GOOD`, `STALE`, `TIMEOUT`, `ERROR`.
- **Connection Manager**: module sở hữu một kết nối MQTT và lifecycle reconnect.
- **Publisher Engine**: module tạo và gửi các bản tin từ Telemetry Store qua Connection Manager.

## Invariants

- HTTP callback chỉ xác thực, parse, validate và commit cấu hình; không trực tiếp start/stop task hoặc chiếm RS485.
- Một RS485 port chỉ có một owner.
- Owner thuộc service, không thuộc TCP client/session; client disconnect không tự release owner khi service vẫn RUNNING.
- `saved=true` không đồng nghĩa `applied=true`.
- MQTT publisher không gọi Modbus trực tiếp trong lúc publish.
- Dashboard không hiển thị `Running` nếu firmware chưa xác nhận runtime state.
- cJSON DOM chỉ tồn tại trong Config Core transaction; runtime module không giữ pointer vào DOM.
