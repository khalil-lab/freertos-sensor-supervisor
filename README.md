# FreeRTOS Sensor Supervisor

A small industrial-style supervision application built on FreeRTOS, running on
the POSIX port so it can be developed and tested on a normal Linux machine.

The application simulates a motor monitoring node: a sensor task samples
vibration, temperature, and current; a protection task reacts to dangerous
conditions; a processing task can fall behind; and a logger drains a separate
queue without blocking the control path.

## Architecture

```text
 sensor_task (prio 3)          processing_task (prio 2)
        |                              |
        | sensor_queue (32)            |
        +----------------------------->+
        |
        | log_queue (8)                logger_task (prio 1)
        +----------------------------->+

        | alarm_queue (4)
        v
 protection_task (prio 4)  ->  latched motor shutdown

 report_task (prio 5)      ->  waits 10 s, prints test summary, exits
```

| Task | Priority | Role |
| --- | ---: | --- |
| `report_task` | 5 | Owns the test run and terminates the simulation |
| `protection_task` | 4 | Highest functional priority: handles safety alarms |
| `sensor_task` | 3 | Periodic sampling every 10 ms (`vTaskDelayUntil`) |
| `processing_task` | 2 | Simulated processing; deliberately overloaded after 5 s |
| `logger_task` | 1 | Slow consumer (250 ms per record), lowest priority |

Queues are fixed-size and use non-blocking sends for sensor data, so overload
turns into measured drops instead of priority inversion or unbounded latency.

## Scenarios

- **0-2 s**: nominal operation.
- **2-2.6 s**: hazardous sample injection (vibration 8.5, temperature 92 C,
  current 18 A) triggers one latched motor shutdown.
- **After 5 s**: processing work exceeds the 10 ms sensor period, creating queue
  pressure and drops while the protection path stays responsive.

## Build and run

```bash
git clone --recurse-submodules https://github.com/khalil-lab/freertos-sensor-supervisor.git
cd freertos-sensor-supervisor

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/sensor_supervisor
```

Requirements: CMake 3.15+, a C compiler, and POSIX threads.

## Representative output

```text
Industrial Sensor Supervisor started.
Hazard injection: 2 s | Processing overload: 5 s

[PROTECTION] MOTOR SHUTDOWN | sample=200 | response=0 ms
[LOGGER] Stored sample 270

========== TEST SUMMARY ==========
Samples processed : 834
Sensor queue drops: 132
Log queue drops   : 71
Late sensor cycles: 0
Shutdown alarms   : 1
Max alarm response: 0 ms
==================================
```

The numbers vary slightly between runs, but the important property is stable:
the protection task meets its deadline and the alarm is raised even while the
processing pipeline is saturated.

## Configuration notes

- `config/FreeRTOSConfig.h` is vendored so the build does not depend on a
  modified file inside the upstream submodule.
- Heap is set to 128 KiB to accommodate five tasks with 2 KiB stacks under the
  POSIX port.
- `configMAX_PRIORITIES` is 6 to leave room for the dedicated report task above
  the protection task.

## License

MIT. See [LICENSE](LICENSE). FreeRTOS-Kernel is licensed separately under MIT.
