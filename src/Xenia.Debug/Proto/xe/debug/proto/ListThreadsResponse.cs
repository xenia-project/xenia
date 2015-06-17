// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListThreadsResponse : Table {
  public static ListThreadsResponse GetRootAsListThreadsResponse(ByteBuffer _bb) { return GetRootAsListThreadsResponse(_bb, new ListThreadsResponse()); }
  public static ListThreadsResponse GetRootAsListThreadsResponse(ByteBuffer _bb, ListThreadsResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListThreadsResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public Thread GetThread(int j) { return GetThread(new Thread(), j); }
  public Thread GetThread(Thread obj, int j) { int o = __offset(4); return o != 0 ? obj.__init(__indirect(__vector(o) + j * 4), bb) : null; }
  public int ThreadLength { get { int o = __offset(4); return o != 0 ? __vector_len(o) : 0; } }

  public static int CreateListThreadsResponse(FlatBufferBuilder builder,
      int thread = 0) {
    builder.StartObject(1);
    ListThreadsResponse.AddThread(builder, thread);
    return ListThreadsResponse.EndListThreadsResponse(builder);
  }

  public static void StartListThreadsResponse(FlatBufferBuilder builder) { builder.StartObject(1); }
  public static void AddThread(FlatBufferBuilder builder, int threadOffset) { builder.AddOffset(0, threadOffset, 0); }
  public static int CreateThreadVector(FlatBufferBuilder builder, int[] data) { builder.StartVector(4, data.Length, 4); for (int i = data.Length - 1; i >= 0; i--) builder.AddOffset(data[i]); return builder.EndVector(); }
  public static void StartThreadVector(FlatBufferBuilder builder, int numElems) { builder.StartVector(4, numElems, 4); }
  public static int EndListThreadsResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
