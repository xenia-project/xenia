// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class XObject : Struct {
  public XObject __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint Handle { get { return bb.GetUint(bb_pos + 0); } }

  public static int CreateXObject(FlatBufferBuilder builder, uint Handle) {
    builder.Prep(4, 4);
    builder.PutUint(Handle);
    return builder.Offset;
  }
};


}
