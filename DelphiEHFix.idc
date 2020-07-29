// ============================================================================
// File:
//   DelphiSEH.idc (for Delphi/C++Builder)
//
// Created by:
//   tulipfan (tulipfan@163.com), HTC (VinCSS)
//
// Purpose:
//   Fixup Delphi's try..finally and try..except statements
//   Hide try/finally/except/end blocks
//
// Usage:
//   Just run the script ;)
//
// History:
//   tfx - 2004-03-11 - 1st version
//   HTC - 2020-06-19 - ver 1.1
//                      Fix finding HandleFinally and HandleAnyException functions
//                      Add support for x64 Delphi binary
//                      Port to IDA 7x IDC style
// ============================================================================

#include <idc.idc>

static MakeCodeEx(ea)
{
    auto i;
    auto Opnd, Unkn, Code;

    i = 0;
    Opnd = GetOpnd(ea, 0);
    if ((GetMnem(ea) == "push") && (substr(Opnd, 0, 9) == "offset aS"))
    {
        Unkn = LocByNameEx(ea, substr(Opnd, 7, -1));
        MakeUnkn(Unkn, 0);
        i++;
        Message("MakeCode at %6X\n", ea);
        while (1)
        {
            Code = MakeCode(Unkn);
            if (Code == 0)
            {
                break;
            }
            Unkn = Unkn + Code;
        }
    }
    return i;
}

static FindRet(adr)
{
    auto a, b;

    for (a = adr; a != BADADDR; a++)
    {
        b = Byte(a);
        if (b == 0xC2)
            return (a + 3);
        else if (b == 0xC3)
            return a + 1;
    }

    return adr;
}

static fixFirst(void)
{
    auto i;
    auto x, ea, ea1;
    auto segName;

    i = 0;
    for (x = get_first_seg(); x != BADADDR; x = get_next_seg(x))
    {
        segName = get_segm_name(x);
        // HTC - TODO: fix if segment name stranged, not hardcoded
        if ((segName == "CODE") || ("code" == segName) || (".text" == segName))
        {
            for (ea = get_segm_attr(x, SEGATTR_START); ea <= get_segm_attr(x, SEGATTR_END); ea = ea + 14)
            {
                ea1 = find_binary(ea, SEARCH_DOWN | SEARCH_NEXT | SEARCH_REGEX, "33 ? 55 68 ? ? ? ? 64 FF ? 64 89 ?");
                if (ea1 < ea)
                {
                    break;  // Find done
                }

                if (ea1 != BADADDR)
                {
                    i = i + MakeCodeEx(ea1 + 3);
                }
            }
        }
    }

    return i;
}

static fix_try_finally(void)
{
    auto i;
    auto x, ea;
    auto _try, _finally, _end;

    x = LocByName("@System@@HandleFinally$qqrv");
    if (x == BADADDR)
        x = LocByName("@@HandleFinally$qqrv");
    if (x != BADADDR)
    {
        Message("%08X: HandleFinally found\n", x);
        i = 0;
        for (ea = RfirstB(x); ea != BADADDR; ea = RnextB(x, ea))
        {
            _try = DfirstB(ea);
            if ((_try != BADADDR) && (XrefType() == dr_O))
            {
                HideArea(_try - 3, _try + 11, form("%6X_try", _try - 3), "try_header", "try_footer", -1);
            }

            _finally = Rfirst(ea + 5);
            if ((_finally != BADADDR) && (XrefType() == fl_JN))
            {
                HideArea(_finally - 13, _finally, form("%6X_finally", _try - 3), "finally_header", "finally_footer", -1);
            }

            _end = ea;
            HideArea(_end - 1, _end + 7, form("%6X_end", _try - 3), "end_header", "end_footer", -1);

            Message("%08X: try_finally block\n", _try - 3);
            i++;
        }
    }
    else
    {
        Message("HandleFinally function address not found\n");
    }

    return i;
}

static fix_try_except(void)
{
    auto i;
    auto x, ea;
    auto _try, _except, _end;

    x = LocByName("@@HandleAnyException");
    if (BADADDR == x)
        x = LocByName("@@System@HandleAnyException");
    if (x != BADADDR)
    {
        Message("%08X: HandleAnyException found\n", x);
        i = 0;
        for (ea = RfirstB(x); ea != BADADDR; ea = RnextB(x, ea))
        {
            _try = DfirstB(ea);
            if ((_try != BADADDR) && (XrefType() == dr_O))
            {
                HideArea(_try - 3, _try + 11, form("%6X_try", _try - 3), "try_header", "try_footer", -1);
            }

            _except = ea;
            HideArea(_except - 10, _except + 5, form("%6X_except", _try - 3), "except_header", "_except_footer", -1);

            _end = Rfirst(ea - 2);
            if ((_except != BADADDR) && (XrefType() == fl_JN))
            {
                HideArea(_end - 5, _end, form("%6X_end", _try - 3), "end_header", "end_footer", -1);
            }

            Message("%08X: try_except block\n", _try - 3);
            i++;
        }
    }
    else
    {
        Message("HandleAnyException function address not found\n");
    }

    return i;
}

static fixFinal()
{
    auto ea;

    for (ea = MinEA(); ea <= MaxEA(); ea++)
    {
        auto b, a1, a2, a3, a4, r, n, retadr;

        if ((ea % 0x100000) == 0)
        {
            Message("\n.%08X", ea);
        }

        if ((ea % 0x8000) == 0)
        {
            Message(".");
        }

        // ea:       push offset a4
        // ea+5:     ...
        // a1(a4-8): retn
        // a2(a4-7): jmp HandleFinally
        // a3(a4-2): jmps xx
        // a4:

        b = Byte(ea);
        if (b != 0x68)
        {
            continue; //push offset a4
        }

        a4 = Dword(ea + 1);
        if ((a4 <= ea) || (a4 - ea > 0x100))
        {
            continue;
        }

        a1 = a4 - 8;
        b = Byte(a1);
        if (b != 0xC3)
        {
            continue; //retn
        }

        a2 = a4 - 7;
        b = Byte(a2);
        if (b != 0xE9)
        {
            continue; //jmp xx xx xx xx
        }

        a3 = a4 - 2;
        b = Byte(a3);
        if (b != 0xEB)
        {
            continue; //jmps xx
        }

        if (GetFunctionAttr(ea, FUNCATTR_END) > a4)
        {
            continue;
        }

        n = GetFunctionName(ea);
        if (n == "")
        {
            continue;
        }

        retadr = FindRet(a4);

        r = OpOff(ea, 0, 0);

        if (!isCode(GetFlags(a1)))
        {
            MakeUnkn(a1, 0);
            r = MakeCode(a1);
        }

        MakeUnkn(a2, 0);
        MakeUnkn(a3, 0);
        MakeUnkn(a4, 0);

        r = MakeCode(a2);
        r = MakeCode(a3);
        r = MakeCode(a4);

        r = SetFunctionEnd(ea, retadr);
        Message("\n%08X: Found EH 0x%X..0x%X - append to '%s' - %s\n", ea, ea, retadr, n, (r ? "OK" : "FAILED"));
    }
}

static main(void)
{
    auto oldstatus = SetStatus(IDA_STATUS_THINKING);

    auto ret = fixFirst();
    msg("Fixup try block count: %6d\n", ret);

    ret = fix_try_finally();
    msg("Hide try_finally block count: %6d\n", ret);

    ret = fix_try_except();
    msg("Hide try_except block count: %6d\n", ret);

    ret = fixFinal();

    SetStatus(oldstatus);
    msg("\nDone.\n");
}
