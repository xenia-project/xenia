#include <idc.idc>

static is_bad_name(s)
{
  auto p;

  if (s == "")
  {
    return 1;
  }

  p = substr(s, 0, 4);
  if (p == "unk_" ||
      p == "loc_" ||
      p == "sub_" ||
      p == "off_" ||
      p == "flt_" ||
      p == "dbl_")
  {
    return 1;
  }

  p = substr(s, 0, 5);
  if (p == "byte_" ||
      p == "word_")
  {
    return 1;
  }

  p = substr(s, 0, 6);
  if (p == "dword_" ||
      p == "qword_")
  {
    return 1;
  }

  p = substr(s, 0, 7);
  if (p == "locret_" ||
      p == "__imp__")
  {
    return 1;
  }

  p = substr(s, 0, 8);
  if (p == "xam_xex_")
  {
    return 1;
  }

  p = substr(s, 0, 9);
  if (p == "xboxkrnl_")
  {
    return 1;
  }

  p = substr(s, 0, 10);
  if (p == "j_xam_xex_")
  {
    return 1;
  }

  p = substr(s, 0, 15);
  if (p == "j_xboxkrnl_exe_")
  {
    return 1;
  }

  return 0;
}

static main()
{
  auto just_code;
  auto seg_start, seg_end, seg_base, seg_name, seg_type;
  auto item_start, item_end, item_flags, item_name;
  auto path, handle;

  seg_start = FirstSeg();
  seg_end = SegEnd(seg_start);
  if (seg_start == BADADDR ||
      seg_end == BADADDR)
  {
    return;
  }

  path = AskFile(1, "*.map", "Save map");
  if (path == "")
  {
    return;
  }


  just_code = AskYN(0, "Just code?") == 1;

  handle = fopen(path, "wb");
  if (!handle)
  {
    return;
  }

  seg_base = 0;
  fprintf(handle, " Start         Length     Name                   Class\n");
  do
  {
    seg_base++;
    seg_name = SegName(seg_start);
    seg_type = GetSegmentAttr(seg_start, SEGATTR_TYPE);

    fprintf(handle, " %04x:%08x %08xH %-23s %s\n", seg_base, -1, seg_end - seg_start, seg_name, seg_type == 2 ? "CODE" : "DATA");

    seg_start = NextSeg(seg_start);
    seg_end = SegEnd(seg_start);
  }
  while (seg_start != BADADDR &&
         seg_end != BADADDR);

  fprintf(handle, "\n");
  fprintf(handle, "  Address         Publics by Value              Rva+Base       Lib:Object\n");

  seg_start = FirstSeg();
  seg_end = SegEnd(seg_start);
  seg_base = 0;
  do
  {
    seg_base++;

    item_start = seg_start;
    while (item_start < seg_end)
    {
      item_end = item_start + 4;
      if (item_end == BADADDR)
      {
        break;
      }

      item_name = GetTrueNameEx(BADADDR, item_start);
      if (is_bad_name(item_name) == 0)
      {
        item_flags = GetFlags(item_start);
        if (just_code == 0 || (item_flags & FF_CODE) == FF_CODE)
        {
          fprintf(handle, " %04x:%08x    %-29s %08x       %s\n", seg_base, item_start - seg_start, item_name, item_start, "<???>");
        }
      }
      item_start = item_start + 4;
    }

    seg_start = NextSeg(seg_start);
    seg_end = SegEnd(seg_start);
  }
  while (seg_start != BADADDR &&
         seg_end != BADADDR);

  fclose(handle);
}
