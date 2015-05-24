// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class StepRequest : Table {
  public static StepRequest GetRootAsStepRequest(ByteBuffer _bb) { return GetRootAsStepRequest(_bb, new StepRequest()); }
  public static StepRequest GetRootAsStepRequest(ByteBuffer _bb, StepRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public StepRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public StepAction Action { get { int o = __offset(4); return o != 0 ? (StepAction)bb.GetSbyte(o + bb_pos) : (StepAction)0; } }
  public uint ThreadId { get { int o = __offset(6); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static int CreateStepRequest(FlatBufferBuilder builder,
      StepAction action = 0,
      uint thread_id = 0) {
    builder.StartObject(2);
    StepRequest.AddThreadId(builder, thread_id);
    StepRequest.AddAction(builder, action);
    return StepRequest.EndStepRequest(builder);
  }

  public static void StartStepRequest(FlatBufferBuilder builder) { builder.StartObject(2); }
  public static void AddAction(FlatBufferBuilder builder, StepAction action) { builder.AddSbyte(0, (sbyte)(action), 0); }
  public static void AddThreadId(FlatBufferBuilder builder, uint threadId) { builder.AddUint(1, threadId, 0); }
  public static int EndStepRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
