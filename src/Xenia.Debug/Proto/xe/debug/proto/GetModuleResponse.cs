// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class GetModuleResponse : Table {
  public static GetModuleResponse GetRootAsGetModuleResponse(ByteBuffer _bb) { return GetRootAsGetModuleResponse(_bb, new GetModuleResponse()); }
  public static GetModuleResponse GetRootAsGetModuleResponse(ByteBuffer _bb, GetModuleResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public GetModuleResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public Module Module { get { return GetModule(new Module()); } }
  public Module GetModule(Module obj) { int o = __offset(4); return o != 0 ? obj.__init(__indirect(o + bb_pos), bb) : null; }

  public static int CreateGetModuleResponse(FlatBufferBuilder builder,
      int module = 0) {
    builder.StartObject(1);
    GetModuleResponse.AddModule(builder, module);
    return GetModuleResponse.EndGetModuleResponse(builder);
  }

  public static void StartGetModuleResponse(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddModule(FlatBufferBuilder builder, int moduleOffset) { builder.AddOffset(0, moduleOffset, 0); }
  public static int EndGetModuleResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
