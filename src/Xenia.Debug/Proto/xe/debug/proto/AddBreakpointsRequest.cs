// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AddBreakpointsRequest : Table {
  public static AddBreakpointsRequest GetRootAsAddBreakpointsRequest(ByteBuffer _bb) { return GetRootAsAddBreakpointsRequest(_bb, new AddBreakpointsRequest()); }
  public static AddBreakpointsRequest GetRootAsAddBreakpointsRequest(ByteBuffer _bb, AddBreakpointsRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AddBreakpointsRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public Breakpoint GetBreakpoints(int j) { return GetBreakpoints(new Breakpoint(), j); }
  public Breakpoint GetBreakpoints(Breakpoint obj, int j) { int o = __offset(4); return o != 0 ? obj.__init(__vector(o) + j * 4, bb) : null; }
  public int BreakpointsLength { get { int o = __offset(4); return o != 0 ? __vector_len(o) : 0; } }

  public static int CreateAddBreakpointsRequest(FlatBufferBuilder builder,
      int breakpoints = 0) {
    builder.StartObject(1);
    AddBreakpointsRequest.AddBreakpoints(builder, breakpoints);
    return AddBreakpointsRequest.EndAddBreakpointsRequest(builder);
  }

  public static void StartAddBreakpointsRequest(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddBreakpoints(FlatBufferBuilder builder, int breakpointsOffset) { builder.AddOffset(0, breakpointsOffset, 0); }
  public static void StartBreakpointsVector(FlatBufferBuilder builder, int numElems) { builder.StartVector(4, numElems, 4); }
  public static int EndAddBreakpointsRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
