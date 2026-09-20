# Anchor for upstream's component-relative include

`bk_wifi_adapter.c:22` reaches the lwIP port header by climbing out of its own
component:

```c
#include <../../lwip_intf_v2_1/lwip-2.1.2/port/net.h>
```

Upstream that works because the file compiles with
`components/bk_wifi/include` on `-I`, and `include/../..` lands on
`components/`, where `lwip_intf_v2_1` is a sibling component.

An angle-bracket include is resolved by appending the *whole* relative path to
each `-I` directory, so to make it land in our replacement layer we need an
`-I` directory two levels below `glue/include`:

```
glue/include/_relpath_anchor/level2/../../lwip_intf_v2_1/lwip-2.1.2/port/net.h
= glue/include/lwip_intf_v2_1/lwip-2.1.2/port/net.h        <- our bridge header
```

Hence `level2/` exists only to be that `-I` anchor; it holds no headers.

The alternative would have been to place the header under
`third_party/beken_armino/glue/lwip_intf_v2_1/...`, where upstream's relative
path resolves with no extra `-I`. It is rejected on ownership grounds: that
header is self-developed (it re-points lwIP netif lifecycle calls at NuttX
netdev), and `third_party/` stays vendored-only. Editing the vendored
`#include` line instead was rejected for the same reason.
