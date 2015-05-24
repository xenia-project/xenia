// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class TableTest : Table {
  public static TableTest GetRootAsTableTest(ByteBuffer _bb) { return GetRootAsTableTest(_bb, new TableTest()); }
  public static TableTest GetRootAsTableTest(ByteBuffer _bb, TableTest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public TableTest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public StructTest St { get { return GetSt(new StructTest()); } }
  public StructTest GetSt(StructTest obj) { int o = __offset(4); return o != 0 ? obj.__init(o + bb_pos, bb) : null; }
  public byte GetIv(int j) { int o = __offset(6); return o != 0 ? bb.Get(__vector(o) + j * 1) : (byte)0; }
  public int IvLength { get { int o = __offset(6); return o != 0 ? __vector_len(o) : 0; } }
  public string Name { get { int o = __offset(8); return o != 0 ? __string(o + bb_pos) : null; } }
  public uint Id { get { int o = __offset(10); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static void StartTableTest(FlatBufferBuilder builder) { builder.StartObject(4); }
  public static void AddSt(FlatBufferBuilder builder, int stOffset) { builder.AddStruct(0, stOffset, 0); }
  public static void AddIv(FlatBufferBuilder builder, int ivOffset) { builder.AddOffset(1, ivOffset, 0); }
  public static int CreateIvVector(FlatBufferBuilder builder, byte[] data) { builder.StartVector(1, data.Length, 1); for (int i = data.Length - 1; i >= 0; i--) builder.AddByte(data[i]); return builder.EndVector(); }
  public static void StartIvVector(FlatBufferBuilder builder, int numElems) { builder.StartVector(1, numElems, 1); }
  public static void AddName(FlatBufferBuilder builder, int nameOffset) { builder.AddOffset(2, nameOffset, 0); }
  public static void AddId(FlatBufferBuilder builder, uint id) { builder.AddUint(3, id, 0); }
  public static int EndTableTest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    builder.Required(o, 8);  // name
    return o;
  }
};


}
