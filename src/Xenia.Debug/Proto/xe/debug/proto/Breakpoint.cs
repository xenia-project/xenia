// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class Breakpoint : Struct {
  public Breakpoint __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint BreakpointId { get { return bb.GetUint(bb_pos + 0); } }

  public static int CreateBreakpoint(FlatBufferBuilder builder, uint BreakpointId) {
    builder.Prep(4, 4);
    builder.PutUint(BreakpointId);
    return builder.Offset;
  }
};


}
