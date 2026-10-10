import argparse
import os
import sys
import time
import requests

DEFAULT_CHUNK_SIZE = 1024
DEFAULT_CONNECT_TIMEOUT = 3.0
DEFAULT_READ_TIMEOUT = 15.0


def upload_file(ip, file_path, target_name, username, password,
                chunk_size=DEFAULT_CHUNK_SIZE,
                connect_timeout=DEFAULT_CONNECT_TIMEOUT,
                read_timeout=DEFAULT_READ_TIMEOUT):
    try:
        file_size = os.path.getsize(file_path)
        print(f"Uploading {file_path} ({file_size} bytes) to {target_name}...")

        session = requests.Session()
        session.auth = (username, password)
        session.headers.update({"Content-Type": "application/octet-stream"})

        with open(file_path, 'rb') as f:
            offset = 0
            while offset < file_size:
                data = f.read(chunk_size)
                if not data:
                    break

                url = f"http://{ip}/api/fs/upload?file={target_name}&offset={offset}"
                try:
                    response = session.post(
                        url,
                        data=data,
                        timeout=(connect_timeout, read_timeout),
                    )
                except requests.RequestException as exc:
                    print(f"Upload stopped at offset {offset}: {exc}")
                    print("The gateway may have committed this chunk; verify serial log before resuming.")
                    return False

                if response.status_code != 200:
                    print(f"Failed at offset {offset}! HTTP {response.status_code}: {response.text}")
                    return False

                try:
                    committed = int(response.text.strip())
                except ValueError:
                    print(f"Invalid upload response at offset {offset}: {response.text!r}")
                    return False
                expected = offset + len(data)
                if committed != expected:
                    print(f"Upload offset mismatch: gateway={committed}, expected={expected}")
                    return False

                offset += len(data)
                print(f"  Progress: {offset}/{file_size} bytes ({(offset/file_size)*100:.1f}%)")

                # Give the HTTP task a scheduling opportunity between flash writes.
                time.sleep(0.02)

        session.close()
        if offset != file_size:
            print(f"Upload incomplete: {offset}/{file_size} bytes")
            return False
        print(f"Success! {target_name} uploaded completely.")
        return True
    except Exception as e:
        print(f"Error uploading {file_path}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Upload web files to GatewayV2 LittleFS")
    parser.add_argument("ip", help="Gateway IP address")
    parser.add_argument("--file", default="dashboard_v16.html",
                        help="File in web_root to upload (default: dashboard_v16.html)")
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="admin")
    parser.add_argument("--chunk-size", type=int, default=DEFAULT_CHUNK_SIZE)
    args = parser.parse_args()

    if args.chunk_size < 256 or args.chunk_size > 4096:
        parser.error("--chunk-size must be between 256 and 4096")

    web_dir = os.path.join(os.path.dirname(__file__), "..", "web_root")
    file_path = os.path.join(web_dir, args.file)
    if not os.path.isfile(file_path):
        print(f"Error: file not found: {file_path}")
        return 1

    print(f"Starting upload to Gateway at {args.ip}...")
    ok = upload_file(args.ip, file_path, args.file, args.user, args.password,
                     chunk_size=args.chunk_size)
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
