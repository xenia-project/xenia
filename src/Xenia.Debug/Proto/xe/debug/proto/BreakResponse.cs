// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class BreakResponse : Table {
  public static BreakResponse GetRootAsBreakResponse(ByteBuffer _bb) { return GetRootAsBreakResponse(_bb, new BreakResponse()); }
  public static BreakResponse GetRootAsBreakResponse(ByteBuffer _bb, BreakResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public BreakResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartBreakResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndBreakResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
