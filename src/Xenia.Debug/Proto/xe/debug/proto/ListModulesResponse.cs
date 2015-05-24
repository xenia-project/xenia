// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListModulesResponse : Table {
  public static ListModulesResponse GetRootAsListModulesResponse(ByteBuffer _bb) { return GetRootAsListModulesResponse(_bb, new ListModulesResponse()); }
  public static ListModulesResponse GetRootAsListModulesResponse(ByteBuffer _bb, ListModulesResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListModulesResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint GetModuleIds(int j) { int o = __offset(4); return o != 0 ? bb.GetUint(__vector(o) + j * 4) : (uint)0; }
  public int ModuleIdsLength { get { int o = __offset(4); return o != 0 ? __vector_len(o) : 0; } }

  public static int CreateListModulesResponse(FlatBufferBuilder builder,
      int module_ids = 0) {
    builder.StartObject(1);
    ListModulesResponse.AddModuleIds(builder, module_ids);
    return ListModulesResponse.EndListModulesResponse(builder);
  }

  public static void StartListModulesResponse(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddModuleIds(FlatBufferBuilder builder, int moduleIdsOffset) { builder.AddOffset(0, moduleIdsOffset, 0); }
  public static int CreateModuleIdsVector(FlatBufferBuilder builder, uint[] data) { builder.StartVector(4, data.Length, 4); for (int i = data.Length - 1; i >= 0; i--) builder.AddUint(data[i]); return builder.EndVector(); }
  public static void StartModuleIdsVector(FlatBufferBuilder builder, int numElems) { builder.StartVector(4, numElems, 4); }
  public static int EndListModulesResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
