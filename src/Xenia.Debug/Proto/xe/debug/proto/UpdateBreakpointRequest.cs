// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class UpdateBreakpointRequest : Table {
  public static UpdateBreakpointRequest GetRootAsUpdateBreakpointRequest(ByteBuffer _bb) { return GetRootAsUpdateBreakpointRequest(_bb, new UpdateBreakpointRequest()); }
  public static UpdateBreakpointRequest GetRootAsUpdateBreakpointRequest(ByteBuffer _bb, UpdateBreakpointRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public UpdateBreakpointRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartUpdateBreakpointRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndUpdateBreakpointRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
