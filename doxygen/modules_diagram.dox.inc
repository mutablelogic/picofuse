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
  Format     [label="Formatting and Output",       URL="\ref SystemFormat"];
  Random     [label="Random Number Generation",     URL="\ref SystemRandom"];
  Thread     [label="Thread and Sleep Operations",  URL="\ref SystemThread"];
  Time       [label="Date and Time Operations",     URL="\ref SystemTime"];
  Data        [label="Data types and utilities", URL="\ref SystemData"];
  DataRune    [label="Runes",                    URL="\ref SystemDataRune"];
  DataString  [label="Strings",                  URL="\ref SystemDataString"];
  DataScanner [label="Scanner",                  URL="\ref SystemDataScanner"];
  DataStream  [label="Stream I/O",               URL="\ref SystemDataStream"];
  DataHash    [label="Hashes",                   URL="\ref SystemDataHash"];

  System -> Sync;
  System -> Format;
  System -> Random;
  System -> Thread;
  System -> Time;
  System -> Data;
  Sync -> Mutex;
  Sync -> Cond;
  Sync -> WaitGroup;
  Sync -> Atomic;
  Data -> DataRune;
  Data -> DataString;
  Data -> DataScanner;
  Data -> DataStream;
  Data -> DataHash;
}
\enddot
