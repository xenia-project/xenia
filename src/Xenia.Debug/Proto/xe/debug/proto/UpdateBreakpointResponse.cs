// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class UpdateBreakpointResponse : Table {
  public static UpdateBreakpointResponse GetRootAsUpdateBreakpointResponse(ByteBuffer _bb) { return GetRootAsUpdateBreakpointResponse(_bb, new UpdateBreakpointResponse()); }
  public static UpdateBreakpointResponse GetRootAsUpdateBreakpointResponse(ByteBuffer _bb, UpdateBreakpointResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public UpdateBreakpointResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartUpdateBreakpointResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndUpdateBreakpointResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
