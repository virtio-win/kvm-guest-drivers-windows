typedef ULONGLONG uint64_t;
#define unlink _unlink
#define sleep(x) Sleep(x * 1000)
#define CLOCK_MONOTONIC 0
#define socket(a, b, c) (-1)
#define listen(a, b) (-1)
#define accept(a, b, v) (-1)
#define connect(a, b, c) (-1)
#define bind(a, b, c) (-1)

#define open _open
#define close _close
#define read _read
#define write _write
