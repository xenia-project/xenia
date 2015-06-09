// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListModuleEntry : Struct {
  public ListModuleEntry __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint Handle { get { return bb.GetUint(bb_pos + 0); } }
  public uint FunctionCount { get { return bb.GetUint(bb_pos + 4); } }

  public static int CreateListModuleEntry(FlatBufferBuilder builder, uint Handle, uint FunctionCount) {
    builder.Prep(4, 8);
    builder.PutUint(FunctionCount);
    builder.PutUint(Handle);
    return builder.Offset;
  }
};


}
