# Mockingbird

UCI four-player chess engine.

## Search benchmark

Build and start the diagnostic command interface:

```sh
make build/diagnostic
./build/diagnostic
```

Enter `bench` to search the fixed benchmark corpus. Scores, best moves, node
counts, and the checksum are deterministic for a given build. Elapsed time is
informational and varies between runs.
