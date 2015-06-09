// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListModulesResponse : Table {
  public static ListModulesResponse GetRootAsListModulesResponse(ByteBuffer _bb) { return GetRootAsListModulesResponse(_bb, new ListModulesResponse()); }
  public static ListModulesResponse GetRootAsListModulesResponse(ByteBuffer _bb, ListModulesResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListModulesResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public ListModuleEntry GetEntry(int j) { return GetEntry(new ListModuleEntry(), j); }
  public ListModuleEntry GetEntry(ListModuleEntry obj, int j) { int o = __offset(4); return o != 0 ? obj.__init(__vector(o) + j * 8, bb) : null; }
  public int EntryLength { get { int o = __offset(4); return o != 0 ? __vector_len(o) : 0; } }

  public static int CreateListModulesResponse(FlatBufferBuilder builder,
      int entry = 0) {
    builder.StartObject(1);
    ListModulesResponse.AddEntry(builder, entry);
    return ListModulesResponse.EndListModulesResponse(builder);
  }

  public static void StartListModulesResponse(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddEntry(FlatBufferBuilder builder, int entryOffset) { builder.AddOffset(0, entryOffset, 0); }
  public static void StartEntryVector(FlatBufferBuilder builder, int numElems) { builder.StartVector(8, numElems, 4); }
  public static int EndListModulesResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
