\dot "Module hierarchy"
digraph modules {
  rankdir=LR;
  node [shape=box, style=filled, fontname="Helvetica", fillcolor="#E8ECFB"];
  edge [color="#7F8FBF"];

  System     [label="System Abstractions", URL="\ref System"];
  Sync       [label="Synchronization",      URL="\ref SystemSync"];
  Mutex      [label="Mutex",                URL="\ref SystemSyncMutex"];
  Cond       [label="Condition Variables",  URL="\ref SystemSyncCond"];
  WaitGroup  [label="Wait Groups",          URL="\ref SystemSyncWaitgroup"];
  Atomic     [label="Atomic Operations",    URL="\ref SystemAtomic"];
  Data        [label="Data", URL="\ref SystemData"];
  DataRune    [label="Runes",                    URL="\ref SystemDataRune"];
  DataString  [label="Strings",                  URL="\ref SystemDataString"];
  DataScanner [label="Scanner",                  URL="\ref SystemDataScanner"];
  DataStream  [label="Stream I/O",               URL="\ref SystemDataStream"];
  DataHash    [label="Hashes",                   URL="\ref SystemDataHash"];
  Random      [label="Random Numbers",           URL="\ref SystemRandom"];
  Time        [label="Date and Time",            URL="\ref SystemTime"];
  Format      [label="Formatting",               URL="\ref SystemFormat"];
  Execution   [label="Execution",                URL="\ref Execution"];
  Threads     [label="Threads",                  URL="\ref SystemThread"];
  Timers      [label="Timers",                   URL="\ref SystemTimer"];
  SleepHalt   [label="Sleep and Halt",           URL="\ref SystemSleepHalt"];
  Env         [label="Environment",              URL="\ref SystemEnv"];

  System -> Sync;
  System -> Data;
  System -> Execution;
  Sync -> Mutex;
  Sync -> Cond;
  Sync -> WaitGroup;
  Sync -> Atomic;
  Data -> DataRune;
  Data -> DataString;
  Data -> DataScanner;
  Data -> DataStream;
  Data -> DataHash;
  Data -> Random;
  Data -> Time;
  Data -> Format;
  Execution -> Threads;
  Execution -> Timers;
  Execution -> SleepHalt;
  Execution -> Env;
}
\enddot
