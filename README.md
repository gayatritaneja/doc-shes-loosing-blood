# Hospital scheduler

This project simulates preemptive priority scheduling for two doctors.

## Compile

From the project directory, compile all three C source files into the
`scheduler` executable:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  main.c io.c priority_preemptive.c -o scheduler
```

You can use `clang` or `cc` instead of `gcc`:

```sh
clang -std=c11 -Wall -Wextra -Wpedantic -Werror \
  main.c io.c priority_preemptive.c -o scheduler
```

## Run the program

```sh
./scheduler input.txt PRIORITY_PREEMPTIVE
```

The command prints the scheduling results as CSV. To compile and run in one
step:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
  main.c io.c priority_preemptive.c -o scheduler &&
./scheduler input.txt PRIORITY_PREEMPTIVE
```

The first two lines contain the patient count and time quantum. The remaining
records use five comma-separated fields:

```text
8
2
patient_id,arrival_time,priority,service_time,category
```

Lower numeric priority values are scheduled first. Blank lines and lines
starting with `#` are ignored.