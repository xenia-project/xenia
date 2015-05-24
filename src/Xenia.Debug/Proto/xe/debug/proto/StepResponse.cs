// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class StepResponse : Table {
  public static StepResponse GetRootAsStepResponse(ByteBuffer _bb) { return GetRootAsStepResponse(_bb, new StepResponse()); }
  public static StepResponse GetRootAsStepResponse(ByteBuffer _bb, StepResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public StepResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartStepResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndStepResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
