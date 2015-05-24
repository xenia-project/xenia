// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class GetModuleRequest : Table {
  public static GetModuleRequest GetRootAsGetModuleRequest(ByteBuffer _bb) { return GetRootAsGetModuleRequest(_bb, new GetModuleRequest()); }
  public static GetModuleRequest GetRootAsGetModuleRequest(ByteBuffer _bb, GetModuleRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public GetModuleRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint ModuleId { get { int o = __offset(4); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static int CreateGetModuleRequest(FlatBufferBuilder builder,
      uint module_id = 0) {
    builder.StartObject(1);
    GetModuleRequest.AddModuleId(builder, module_id);
    return GetModuleRequest.EndGetModuleRequest(builder);
  }

  public static void StartGetModuleRequest(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddModuleId(FlatBufferBuilder builder, uint moduleId) { builder.AddUint(0, moduleId, 0); }
  public static int EndGetModuleRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
