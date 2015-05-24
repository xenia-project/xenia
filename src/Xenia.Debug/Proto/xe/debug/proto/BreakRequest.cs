// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class BreakRequest : Table {
  public static BreakRequest GetRootAsBreakRequest(ByteBuffer _bb) { return GetRootAsBreakRequest(_bb, new BreakRequest()); }
  public static BreakRequest GetRootAsBreakRequest(ByteBuffer _bb, BreakRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public BreakRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartBreakRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndBreakRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
