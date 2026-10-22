# telnet_tls — Telnet over TLS console gateway

`telnet_tls_server` exposes the consoles managed by `console_server`
(see `../obmc_console`) over **telnet inside TLS** on TCP port 992
("telnets"). `telnet_tls_client` is the matching client.

```
telnet_tls_client --TLS 992--> telnet_tls_server --unix socket--> console_server
                                  |  telnet IAC layer
                                  |  PAM login
                                  `  console menu (from obmc-console-multi.conf)
```

## Features

- TLS from the first byte (no STARTTLS), TLS >= 1.2, AEAD cipher suites only.
- Username/password login inside the telnet session via PAM
  (service `telnet-tls`, `/etc/pam.d/telnet-tls`), 3 attempts, 2 s delay after
  each failure, 30 s idle timeout while logging in / choosing from the menu.
- PAM runs in a short-lived forked helper so the single-threaded io_context
  (`BOOST_ASIO_DISABLE_THREADS`) is never blocked.
- Optional `--group <name>`: the user must also be a member of that group.
- Menu is built from the `[console.*]` sections of the console config.
  `ibmi-emulator` consoles are skipped (VSLIP/5250 is not usable in a plain
  telnet terminal).
- Telnet options: ECHO, SUPPRESS-GO-AHEAD, NAWS (recorded, not forwarded — the
  console backends have no resize channel), TERMINAL-TYPE.
- Press **Ctrl-]** in a console session to return to the menu.
- Session limit (8 concurrent sessions).

## Build

Built with the rest of coroserver (needs `libpam`; the example is skipped with
a warning if PAM is not found):

```
meson setup build
ninja -C build telnet_tls_server telnet_tls_client
ninja -C build test      # includes the telnet_protocol unit tests
```

The protocol layer unit test only needs Boost.Test:
`ninja -C build tests/test_telnet_protocol && meson test -C build telnet_protocol`.

## Server options

| Option | Default | Meaning |
|---|---|---|
| `--bind`, `-b` | `127.0.0.1` | Address to listen on. Set to the BMC's management address to accept remote connections. |
| `--port`, `-p` | `992` | TCP port |
| `--cert`, `-c` | `/etc/ssl/private/server-cert.pem` | Certificate chain (PEM) |
| `--key`, `-k` | `/etc/ssl/private/server-key.pem` | Private key (PEM) |
| `--config` | `/run/obmc-console/obmc-console-multi.conf`, else `/etc/obmc-console-multi.conf` | Console config shared with `console_server` |
| `--pam-service` | `telnet-tls` | PAM service name |
| `--group` | (none) | Extra group membership required, e.g. `hostconsole` |

Options are passed as `--name value` pairs. With systemd, put them in
`/etc/default/telnet-tls`:

```
TELNET_TLS_ARGS="--bind 10.0.0.5 --group hostconsole"
```

The service runs as root (PAM must read the shadow database). The unix sockets
of `console_server` must be reachable by root; sockets configured with
`socket-group` need no extra step because root bypasses permissions.

## Certificate

Use the same cert/key location as the other coroserver TLS services. For
testing, generate a self-signed pair:

```
openssl req -x509 -newkey rsa:3072 -nodes -days 365 \
  -keyout /etc/ssl/private/server-key.pem \
  -out /etc/ssl/private/server-cert.pem \
  -subj "/CN=<bmc-hostname>" -addext "subjectAltName=DNS:<bmc-hostname>"
chmod 600 /etc/ssl/private/server-key.pem
```

(`include/cert_generator.hpp` can also generate one programmatically.)

## Client

```
telnet_tls_client --host <bmc> [--port 992] [--cafile server-cert.pem]
```

- The server certificate chain and host name are verified against `--cafile`
  or the system trust store.
- `--insecure 1` disables verification. Self-signed testing only.
- Local disconnect: `Enter ~ .`

## Test with standard tools

```
openssl s_client -connect <bmc>:992 -crlf        # TLS handshake + raw telnet
```

A plain `telnet <bmc> 992` cannot complete the TLS handshake and yields no
session.

## Security notes

- Credentials travel only inside TLS; the server never logs passwords and logs
  failed logins by remote address only.
- Binding defaults to loopback. Do not expose the port beyond the management
  network.
- No client-certificate authentication in v1.

## Manual verification checklist

Run on a BMC or qemu image (not on macOS: no PAM / systemd):

1. `console_server` running with a `[console.bash]` pty console.
2. `telnet_tls_server --bind 127.0.0.1 &`
3. `telnet_tls_client --host 127.0.0.1 --cafile /etc/ssl/private/server-cert.pem`
4. Wrong password -> "Login incorrect" (3 tries, then disconnect).
5. Valid user -> menu lists consoles -> select `bash` -> shell works -> Ctrl-]
   returns to the menu -> `q` closes the session.
6. `openssl s_client -connect 127.0.0.1:992` shows TLS 1.2/1.3.
7. `telnet 127.0.0.1 992` gets no usable session.
