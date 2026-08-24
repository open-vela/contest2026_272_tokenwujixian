# BK7258 platform test

Run the test manually from CP NSH:

```text
bk7258_platform_test
```

The test snapshots and restores the clock, power, ICU and TXEN/RXEN mux
registers so invoking it does not leave the normal CP/LCD runtime in a test
specific state.

The bundled CP recovery package contains an AP placeholder. With that package,
`[AP] CPU1 release failed` is expected and is not a platform-test failure. Use
the OpenVela AP package when AP release itself is being tested.
