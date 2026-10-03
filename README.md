# RinURI

RinURI provides bounded URI parsing and normalization for RinOS components.

## Public API contract

| Requirement | Contract |
| --- | --- |
| Purpose | RinURI provides bounded URI parsing and normalization for RinOS components. |
| Supported API | The public C interface is `rinuri/uri.h`. It parses and normalizes URI strings and exposes their components within caller-managed buffers/results, including bracketed IPv6 hosts with an RFC 3986 IPv4 tail such as `[::ffff:192.0.2.1]` and an RFC 6874 zone identifier such as `[fe80::1%25eth0]`. |
| Unsupported API | RinURI does not perform DNS resolution, network access, TLS, origin authorization, or filesystem containment checks. |
| ownership | Input and output storage are caller-owned. Any returned views follow the lifetimes described by the declarations and must not outlive their source/result storage. |
| thread-safety | Independent calls on separate buffers may run concurrently. Shared mutable results and buffers require caller synchronization. |
| limits | URI input is limited to 4096 bytes. Inputs beyond the limit fail parsing. |
| errors | Invalid syntax, insufficient storage, or over-limit input is reported by the parser's return value. Percent-escape and IPv6-zone lookahead checks use subtraction bounds before indexing, and a failed parse clears the caller-owned `RinUri`, so partial scheme/authority/path views are not published; callers must still check the status before using components. |
| ABI stability | `rinuri/uri.h` is the public C ABI. No cross-version ABI stability guarantee is published; rebuild consumers when updating the library. |
| security | A parsed or normalized URI is data, not an authorization decision. Callers must separately apply scheme, host, origin, and network policy before use. |
| build | Consume the public header through the RinOS build; no separate build/install workflow is documented. |
| test | No standalone test command is documented. Validate URI policy in the consuming component. |
