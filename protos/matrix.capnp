@0xbf5147cbbecf40e1;

interface MatrixWorker {
  sendTask @0 (task :MatrixTask);
}

interface MatrixManager {
  start @0 (listener :MatrixWorker);
  submitTask @1 (task :MatrixTask) -> (result :MatrixResult);
}

struct MatrixTask {
  taskId @0 :Int32;
  ops @1 :Text;
  n @2 :Int32;
  inputA @3 :Data;  # used to hold raw double[] data
  inputB @4 :Data;
}

struct MatrixResult {
  taskId @0 :Int32;
  n @1 :Int32;
  result @2 :Data;
}
