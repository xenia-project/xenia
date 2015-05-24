// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class Module : Table {
  public static Module GetRootAsModule(ByteBuffer _bb) { return GetRootAsModule(_bb, new Module()); }
  public static Module GetRootAsModule(ByteBuffer _bb, Module obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public Module __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public XObject Object { get { return GetObject(new XObject()); } }
  public XObject GetObject(XObject obj) { int o = __offset(4); return o != 0 ? obj.__init(o + bb_pos, bb) : null; }
  public ModuleType Type { get { int o = __offset(6); return o != 0 ? (ModuleType)bb.GetSbyte(o + bb_pos) : (ModuleType)0; } }
  public string Name { get { int o = __offset(8); return o != 0 ? __string(o + bb_pos) : null; } }
  public string Path { get { int o = __offset(10); return o != 0 ? __string(o + bb_pos) : null; } }

  public static void StartModule(FlatBufferBuilder builder) { builder.StartObject(4); }
  public static void AddObject(FlatBufferBuilder builder, int objectOffset) { builder.AddStruct(0, objectOffset, 0); }
  public static void AddType(FlatBufferBuilder builder, ModuleType type) { builder.AddSbyte(1, (sbyte)(type), 0); }
  public static void AddName(FlatBufferBuilder builder, int nameOffset) { builder.AddOffset(2, nameOffset, 0); }
  public static void AddPath(FlatBufferBuilder builder, int pathOffset) { builder.AddOffset(3, pathOffset, 0); }
  public static int EndModule(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
