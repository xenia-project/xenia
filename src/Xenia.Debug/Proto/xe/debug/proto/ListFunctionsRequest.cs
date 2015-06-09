// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListFunctionsRequest : Table {
  public static ListFunctionsRequest GetRootAsListFunctionsRequest(ByteBuffer _bb) { return GetRootAsListFunctionsRequest(_bb, new ListFunctionsRequest()); }
  public static ListFunctionsRequest GetRootAsListFunctionsRequest(ByteBuffer _bb, ListFunctionsRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListFunctionsRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint ModuleId { get { int o = __offset(4); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint FunctionIndexStart { get { int o = __offset(6); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint FunctionIndexEnd { get { int o = __offset(8); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static int CreateListFunctionsRequest(FlatBufferBuilder builder,
      uint module_id = 0,
      uint function_index_start = 0,
      uint function_index_end = 0) {
    builder.StartObject(3);
    ListFunctionsRequest.AddFunctionIndexEnd(builder, function_index_end);
    ListFunctionsRequest.AddFunctionIndexStart(builder, function_index_start);
    ListFunctionsRequest.AddModuleId(builder, module_id);
    return ListFunctionsRequest.EndListFunctionsRequest(builder);
  }

  public static void StartListFunctionsRequest(FlatBufferBuilder builder) { builder.StartObject(3); }
  public static void AddModuleId(FlatBufferBuilder builder, uint moduleId) { builder.AddUint(0, moduleId, 0); }
  public static void AddFunctionIndexStart(FlatBufferBuilder builder, uint functionIndexStart) { builder.AddUint(1, functionIndexStart, 0); }
  public static void AddFunctionIndexEnd(FlatBufferBuilder builder, uint functionIndexEnd) { builder.AddUint(2, functionIndexEnd, 0); }
  public static int EndListFunctionsRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
