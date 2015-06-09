// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListFunctionsResponse : Table {
  public static ListFunctionsResponse GetRootAsListFunctionsResponse(ByteBuffer _bb) { return GetRootAsListFunctionsResponse(_bb, new ListFunctionsResponse()); }
  public static ListFunctionsResponse GetRootAsListFunctionsResponse(ByteBuffer _bb, ListFunctionsResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListFunctionsResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public FunctionEntry GetEntry(int j) { return GetEntry(new FunctionEntry(), j); }
  public FunctionEntry GetEntry(FunctionEntry obj, int j) { int o = __offset(4); return o != 0 ? obj.__init(__indirect(__vector(o) + j * 4), bb) : null; }
  public int EntryLength { get { int o = __offset(4); return o != 0 ? __vector_len(o) : 0; } }

  public static int CreateListFunctionsResponse(FlatBufferBuilder builder,
      int entry = 0) {
    builder.StartObject(1);
    ListFunctionsResponse.AddEntry(builder, entry);
    return ListFunctionsResponse.EndListFunctionsResponse(builder);
  }

  public static void StartListFunctionsResponse(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddEntry(FlatBufferBuilder builder, int entryOffset) { builder.AddOffset(0, entryOffset, 0); }
  public static int CreateEntryVector(FlatBufferBuilder builder, int[] data) { builder.StartVector(4, data.Length, 4); for (int i = data.Length - 1; i >= 0; i--) builder.AddOffset(data[i]); return builder.EndVector(); }
  public static void StartEntryVector(FlatBufferBuilder builder, int numElems) { builder.StartVector(4, numElems, 4); }
  public static int EndListFunctionsResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
