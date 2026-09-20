# wpa_supplicant bk_patch forwarding headers

The vendored glue includes four headers that live in
`third_party/beken_armino/wpa_supplicant/bk_patch/`. Upstream puts that whole
directory on the include path, but we cannot: `bk_patch/signal.h` is a 47-line
list of signal numbers with no `sigset_t`, and it shadows libc `<signal.h>`.
GCC searches `-I` before `-isystem`, so any NuttX header chain reaching
`<signal.h>` (e.g. `<unistd.h>` -> `<nuttx/sched.h>` -> `<sys/select.h>`, which
lwIP's `arch.h` pulls in) then fails with "unknown type name 'sigset_t'".

So this directory is on the include path instead of `bk_patch`, and forwards
only the headers the glue actually needs. `bk_patch/signal.h` is deliberately
not forwarded: the two glue files including `"signal.h"` reference nothing from
it, so they resolve to libc's instead.

Headers reached internally from within `bk_patch` (e.g. `sk_intf.h` including
`fake_socket.h`) resolve without forwarding, because a quoted include searches
the including file's own directory first.
