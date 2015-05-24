// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class StructTest : Struct {
  public StructTest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public short A { get { return bb.GetShort(bb_pos + 0); } }
  public sbyte B { get { return bb.GetSbyte(bb_pos + 2); } }
  public Foo C { get { return (Foo)bb.GetSbyte(bb_pos + 3); } }

  public static int CreateStructTest(FlatBufferBuilder builder, short A, sbyte B, Foo C) {
    builder.Prep(2, 4);
    builder.PutSbyte((sbyte)(C));
    builder.PutSbyte(B);
    builder.PutShort(A);
    return builder.Offset;
  }
};


}
