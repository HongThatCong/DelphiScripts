/*
 * This script haves a big deal with Delphi RTTI structures
 *
 * Created by Red Plait (redplait@usa.net), 23-08-1999
 * History:
 * 28-08-1999   RP      Added support for dynamic methods
 * 01-09-1999   RP      Added support for interfaces & eight methods in VTBL
 *                      with negative indexes
 * 06-09-1999   RP      TypeInfo of published properties (rip some code from
 *                      Santucco)
 * 02-09-2004   VK      MakePStr,processOwned - make propnames properly
 * xx-07-2020 HTC-VinCSS    Add some contants and types, fix parsing for >= Delphi 6
 *                          Add support for Delphi x64
 */

#include <idc.idc>

// HTC: Constans from TypeInfo.pas Delphi 2010

// consts for TTypeKind
#define tkUnknown       0
#define tkInteger       1
#define tkChar          2
#define tkEnumeration   3
#define tkFloat         4
#define tkString        5
#define tkSet           6
#define tkClass         7
#define tkMethod        8
#define tkWChar         9
#define tkLString       10
#define tkWString       11
#define tkVariant       12
#define tkArray         13
#define tkRecord        14
#define tkInterface     15
#define tkInt64         16
#define tkDynArray      17
#define tkUString       18  // Add by HTC to below
#define tkClassRef      19
#define tkPointer       20
#define tkProcedure     21

// consts for TOrdType
#define otSByte         0
#define otUByte         1
#define otSWord         2
#define otUWord         3
#define otSLong         4   // HTC
#define otSLong         5

// consts for TFloatType
#define ftSingle        0
#define ftDouble        1
#define ftExtended      2
#define ftComp          3
#define ftCurr          4

// const for TMemberVisibility
#define mvPrivate       0
#define mvProtected     1
#define mvPublic        2
#define mvPublished     3

// consts for TMethodKind
#define mkProcedure         0
#define mkFunction          1
#define mkConstructor       2
#define mkDestructor        3
#define mkClassProcedure    4
#define mkClassFunction     5
#define mkClassConstructor  6
#define mkClassDestructor   7
// HTC - Obsolete, Delphi 2010
#define mkSafeProcedure     8
#define mkSafeFunction      9

// consts for TParamFlag - set
#define pfVar           0
#define pfConst         1
#define pfArray         2
#define pfAddress       3
#define pfReference     4
#define pfOut           5
#define pfResult        6   // HTC

// consts for IntfFlag - set
#define ifHasGuid       0
#define ifDispInterface 1
#define ifDispatch      2

// convert long to upper-case hex string
// ltox(0xabcd)="ABCD"
static ltox(adr)
{
    auto i, s, c, str;

    str = ltoa(adr, 16);
    s = "";

    for (i = 0; i < strlen(str); i++)
    {
        c = substr(str, i, i + 1);

        if (c == "a")
        {
            c = "A";
        }
        else if (c == "b")
        {
            c = "B";
        }
        else if (c == "c")
        {
            c = "C";
        }
        else if (c == "d")
        {
            c = "D";
        }
        else if (c == "e")
        {
            c = "E";
        }
        else if (c == "f")
        {
            c = "F";
        }

        s = s + c;
    }

    return s;
}

// do reenterable comments :-)
// Params:
//  adr       - address to comment
//  set_to    - comment to set
//  is_before - place new comment before old
static setComment(adr, set_to, is_before)
{
    auto old_comm;

    if (!strlen(set_to))
    {
        return;    // no comments
    }

    old_comm = Comment(adr);

    if (!strlen(old_comm))
    {
        MakeComm(adr, set_to);
        return;
    }

    if (-1 != strstr(old_comm, set_to))
    {
        return;
    }

    if (is_before)
    {
        MakeComm(adr, set_to + "\n" + old_comm);
    }
    else
    {
        MakeComm(adr, old_comm + "\n" + set_to);
    }
}

// makes byte
static ReMakeByte(adr)
{
    MakeUnkn(adr, 0);
    MakeByte(adr);
    return Byte(adr);
}

// makes dword
static ReMakeInt(adr)
{
    MakeUnkn(adr, 0);
    MakeUnkn(adr + 1, 0);
    MakeUnkn(adr + 2, 0);
    MakeUnkn(adr + 3, 0);
    MakeDword(adr);
    return Dword(adr);
}

// makes word
static ReMakeWord(adr)
{
    MakeUnkn(adr, 0);
    MakeUnkn(adr + 1, 0);
    MakeWord(adr);
    return Word(adr);
}

// makes qword
static ReMakeQword(adr)
{
    auto count;
    //debug: Message("QWord: "+ltoa(adr,0x10));

    for (count = 0; count < 8; count++)
    {
        MakeUnkn(adr + count, 0);
    }

    MakeQword(adr);

    //return Qword(adr);
    return (Dword(adr) + Dword(adr + 4) * 0x100000000);
}

// makes dword and offset to data
static MakeOffset(adr)
{
    auto ref_adr;
    ref_adr = ReMakeInt(adr);

    if (ref_adr != 0)
    {
        //add_dref(adr,ref_adr,0);
        OpOff(adr, 0, 0);
    }

    return ref_adr;
}

static ReMakeFunc(adr)
{
    if (adr != 0)
    {
        MakeUnkn(adr, 1);
        MakeCode(adr);
        MakeFunction(adr, BADADDR);
    }
}

// makes dword and offset to a function
static MakeFOffset(adr)
{
    auto ref_adr;
    ref_adr = MakeOffset(adr);
    ReMakeFunc(ref_adr);
    return ref_adr;
}

// make offset to function and name it
// if it's address is in address range
// and name is empty or something like
// standard 'sub_', 'unknown_libname_'
static MakeNameFOffset(adr, name)
{
    auto ref_adr, fname;
    ref_adr = MakeOffset(adr);

    if ((!ref_adr) || (ref_adr < MinEA()) || (ref_adr > MaxEA()))
    {
        return 0;
    }

    fname = GetFunctionName(ref_adr);

    if ((fname == "") ||
        (fname != GetTrueName(ref_adr)) ||
        (substr(fname, 0, 4) == "sub_") ||
        (substr(fname, 0, 16) == "unknown_libname_") ||
        (substr(fname, 0, 5) == "@Unit") ||
        (substr(fname, 0, 8) == "nullsub_"))
    {
        ReMakeFunc(ref_adr);
        // MakeName(ref_adr,name);

        if (!MakeName(ref_adr, name))
        {
            name = AskStr(name, "Enter new name:");
            MakeName(ref_adr, name);
        }
    }
}

#define allowed_fchar "abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define allowed_chars "abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

static check_pname(adr)
{
    auto c, count, val, len;
    val = Dword(adr);

    if (!val)
    {
        return 0;
    }

    len = Byte(val);

    if (!len)
    {
        return 0;
    }

    val = val + 1;

    c = Byte(val);

    if (-1 == strstr(allowed_fchar, c))
    {
        return 0;
    }

    len = len - 1;

    val = val + 1;

    for (count = 0; count < len;)
    {
        c = Byte(val);

        if (-1 == strstr(allowed_chars, c))
        {
            return 0;
        }

        count++;

        val++;
    }

    return 1;
}

static check_name(str)
{
    auto pos, len;
    len = strlen(str);

    if (!len)
    {
        return 0;
    }

    if (-1 == strstr(allowed_fchar, substr(str, 0, 1)))
    {
        return 0;
    }

    for (pos = 1; pos < len; pos++)
    {
        if (-1 == strstr(allowed_chars, substr(str, pos, pos + 1)))
        {
            return 0;
        }
    }

    return 1;
}

// makes simple string
static ReMakeStr(adr, len)
{
    auto count, oldstrtype;

    for (count = 0; count < len; count++)
    {
        MakeUnkn(adr + count, 0);
    }

    oldstrtype = GetLongPrm(INF_STRTYPE);
    SetLongPrm(INF_STRTYPE, ASCSTR_PASCAL);
    MakeStr(adr, adr + len);

    SetLongPrm(INF_STRTYPE, oldstrtype);
}

// makes Pascal-style string
// Returns length of pascal string (including byte for length)
static MakePStr(adr)
{
    auto len, ulen, count, oldstrtype;
    MakeUnkn(adr, 0);
    len = ReMakeByte(adr);
    ulen = len + 1;
    //MakeUnkn(adr,0);
    //ReMakeStr(adr+1,len);

    for (count = 0; count < ulen; count++)
    {
        MakeUnkn(adr + count, 0);
    }

    oldstrtype = GetLongPrm(INF_STRTYPE);

    SetLongPrm(INF_STRTYPE, ASCSTR_PASCAL);

    MakeStr(adr, adr + ulen);

    SetLongPrm(INF_STRTYPE, oldstrtype);

    return ulen;
}

// extract pascal-style string
static GetPStr(adr)
{
    auto len, res, c;
    len = Byte(adr++);
    res = "";

    for (; len; len--)
    {
        c = Byte(adr++);
        res = res + c;
    }

    return res;
}

// returns name of class of this RTTI
static getRTTIName(adr)
{
    auto ptr;
    ptr = Dword(adr + 0x20);

    if (ptr != 0)
    {
        return GetPStr(ptr);
    }
    else
    {
        return "";
    }
}

static getOwnedCount(adr)
{
    return Word(Dword(adr + 2)); // wow!
}

// processing owned components list
// Returns ptr to RTTI array (cauze I don`t know how to make forward declaration
//  of _processRTTI
static processOwned(adr)
{
    auto count, str_len, comp_count, rtti_base;

    comp_count = ReMakeWord(adr);  /* count of RTTI array */
    adr = adr + 2;
    rtti_base = MakeOffset(adr);   /* offset to array of RTTI */
    adr = adr + 4;

    /* process RTTI array */
    count = ReMakeWord(rtti_base); /* size of array */
    rtti_base = rtti_base + 2;

    for (str_len = 0; str_len < count; str_len++)
    {
        MakeOffset(rtti_base + str_len * 4);
    }

    /* process each of owned to form components */
    for (count = 0; count < comp_count; count++)
    {
        // offset in owners class
        str_len = ReMakeWord(adr);
        setComment(adr, "Offset 0x" + ltoa(str_len, 0x10), 1);
        adr = adr + 2;

        // unknown word
        ReMakeWord(adr);
        adr = adr + 2;

        // index in RTTI array
        str_len = ReMakeWord(adr);
        setComment(adr, "Type: " + getRTTIName(Dword(rtti_base + str_len * 4)), 1);
        adr = adr + 2;

        // pascal string - name of component
        //str_len = ReMakeByte(adr);
        //adr = adr + 1;
        //ReMakeStr(adr,str_len);
        //adr = adr + str_len;
        adr = adr + MakePStr(adr);
    }

    return rtti_base;
}

// process event handlers list
static processHandlers(adr, class_name)
{
    auto count, str_len;

    count = ReMakeWord(adr);
    setComment(adr, "Handlers count", 1);
    adr = adr + 2;

    for (; count; count--)
    {
        // unknown dword
        ReMakeWord(adr);

        // offset to function - handler
        adr = adr + 2;
        MakeNameFOffset(adr, "@" + class_name + "@" + GetPStr(adr + 4));
        setComment(adr, "@" + class_name + "@"  + GetPStr(adr + 4), 1);

        // Name of handler
        adr = adr + 4;
        adr = adr + MakePStr(adr);
    }
}

// returns name of type published property
static get_type_name(adr)
{
    auto deref;

    deref = Dword(adr);
    if (deref)
    {
        return GetPStr(deref + 1);
    }
    else
    {
        return "";
    }
}

#define encodeKind(c, name)     if (Ord == c) return name;

static TypeOrdComm(Ord)
{
    encodeKind(otSByte, "Signed Byte");
    encodeKind(otUByte, "Unsigned Byte");
    encodeKind(otSWord, "Signed Word");
    encodeKind(otUWord, "Unsigned Word");
    encodeKind(otSLong, "Signed Long");

    return "";
}

static TypeOrdKind(Ord)
{
    encodeKind(tkUnknown, "Unknown");
    encodeKind(tkInteger, "Integer");
    encodeKind(tkChar, "Char");
    encodeKind(tkEnumeration, "Enum");
    encodeKind(tkFloat, "Float");
    encodeKind(tkString, "string");
    encodeKind(tkSet, "set");
    encodeKind(tkClass, "class");
    encodeKind(tkMethod, "method");
    encodeKind(tkWChar, "WChar");
    encodeKind(tkLString, "LString");
    encodeKind(tkWString, "WString");
    encodeKind(tkVariant, "Variant");
    encodeKind(tkArray, "array");
    encodeKind(tkRecord, "record");
    encodeKind(tkInterface, "interface");
    encodeKind(tkInt64, "Int64");
    encodeKind(tkDynArray, "DynArray");
    encodeKind(tkUString, "UnicodeString");     // Add by HTC to below
    encodeKind(tkClassRef, "Class reference");
    encodeKind(tkPointer, "Pointer");
    encodeKind(tkProcedure, "procedure");

    return "";
}

static TypeOrdFloat(Ord)
{
    encodeKind(ftSingle, "Single");
    encodeKind(ftDouble, "Double");
    encodeKind(ftExtended, "Extended");
    encodeKind(ftComp, "Comp");
    encodeKind(ftCurr, "Currency");

    return "";
}

#define encodeSet(c,name)   if ((1 << c) & Set) { \
    if (has_value) \
        res = res + "," + name; \
    else \
    {\
        res = name;\
        has_value = 1;\
    }\
}

static doParamFlag(Set)
{
    auto res, has_value;

    res = "";
    has_value = 0;
    encodeSet(pfVar, "var");
    encodeSet(pfConst, "const");
    encodeSet(pfArray, "array");
    encodeSet(pfAddress, "address");
    encodeSet(pfReference, "reference");
    encodeSet(pfOut, "out");

    if (strlen(res))
    {
        return "[" + res + "]";
    }
    else
    {
        return "";
    }
}

static doIntfFlag(Set)
{
    auto res, has_value;

    res = "";
    has_value = 0;
    encodeSet(ifHasGuid, "hasGuid");
    encodeSet(ifDispInterface, "dispInterface");
    encodeSet(ifDispatch, "dispatch");

    if (strlen(res))
    {
        return "[" + res + "]";
    }
    else
    {
        return "";
    }
}

static TypeOrdMethod(Ord)
{
    encodeKind(mkProcedure, "procedure");
    encodeKind(mkFunction, "function");
    encodeKind(mkConstructor, "constructor");
    encodeKind(mkDestructor, "destructor");
    encodeKind(mkClassProcedure, "classProcedure");
    encodeKind(mkClassFunction, "classFunction");
    encodeKind(mkSafeProcedure, "safeProcedure");
    encodeKind(mkSafeFunction, "safeFunction");
    return "";
}

/* process TypeInfo struct */
static processTypeInfo(adr)
{
    /*
     *  TTypeInfo=record
     *    Kind:TTypeKind;
     *    Name:ShortString;
     *    {TypeData: TTypeData}
     *  end;
     *
     *  PTypeData=^TTypeData;
     *  TTypeData=packed record
     *    case TTypeKind of
     *      tkUnknown,tkLString,tkWString,tkVariant:();
     *      tkInteger,tkChar,tkEnumeration,tkSet,tkWChar:(
     *        OrdType:TOrdType;
     *        case TTypeKind of
     *          tkInteger,tkChar,tkEnumeration,tkWChar:(
     *            MinValue:Longint;
     *            MaxValue:Longint;
     *            case TTypeKind of
     *              tkInteger,tkChar,tkWChar:();
     *              tkEnumeration:(
     *                BaseType:PPTypeInfo;
     *                NameList:ShortStringBase));
     *          tkSet:(
     *            CompType:PPTypeInfo));
     *      tkFloat: (
     *        FloatType: TFloatType);
     *      tkString: (
     *        MaxLength: Byte);
     *      tkClass: (
     *        ClassType: TClass;
     *        ParentInfo: PPTypeInfo;
     *        PropCount: SmallInt;
     *        UnitName: ShortStringBase;
     *       {PropData: TPropData});
     *      tkMethod: (
     *        MethodKind: TMethodKind;
     *        ParamCount: Byte;
     *        ParamList: array[0..1023] of Char
     *       {ParamList: array[1..ParamCount] of
     *          record
     *            Flags: TParamFlags;
     *            ParamName: ShortString;
     *            TypeName: ShortString;
     *          end;
     *        ResultType: ShortString});
     *      tkInterface: (
     *        IntfParent : PPTypeInfo; { ancestor }
     *        IntfFlags : TIntfFlagsBase;
     *        Guid : TGUID;
     *        IntfUnit : ShortStringBase;
     *       {PropData: TPropData});
     *      tkInt64: (
     *        MinInt64Value, MaxInt64Value: Int64);
     *  end;
     * Wow! IDA understand such structs ?
     */
    auto Kind, start_TI, ord, count;
    auto min_value, max_value;

    adr = MakeOffset(adr);

    if (!adr)
    {
        return;
    }

    start_TI = adr;

    /* Kind of TypeInfo */
    Kind = ReMakeByte(adr);

    setComment(adr, TypeOrdKind(Kind), 1);

    adr = adr + 1;

    /* name */
    adr = adr + MakePStr(adr);

    /* next begin horror */
    if (Kind == tkUnknown || Kind == tkLString || Kind == tkWString || Kind == tkVariant)
    {
        return;
    }

    if (Kind == tkInteger || Kind == tkChar || Kind == tkEnumeration || Kind == tkSet || Kind == tkWChar)
    {
        ord = ReMakeByte(adr);
        setComment(adr, "Ord " + TypeOrdComm(ord), 1);
        adr = adr + 1;

        if (Kind != tkSet)
        {
            min_value = ReMakeInt(adr);
            setComment(adr, "MinValue", 1);
            adr = adr + 4;
            max_value = ReMakeInt(adr);
            setComment(adr, "MaxValue", 1);
            adr = adr + 4;

            if (tkEnumeration == Kind)
            {
                MakeOffset(adr);
                setComment(adr, "BaseType", 1);
                adr = adr + 4;
                /* wierd bug with wrong Enum ranges. Thanx to Santucco */

                if (max_value >= min_value)
                    for (count = max_value - min_value + 1; count; count--)
                    {
                        adr = adr + MakePStr(adr);
                    }
            }
        }
        else
        {
            // for tsSet
            adr = MakeOffset(adr);
            setComment(adr, "CompType", 1);

            if (adr)
            {
                processTypeInfo(adr);    // CompType is ptr to PTypeInfo again
            }
        }

        return;
    }

    if (Kind == tkFloat)
    {
        ord = ReMakeInt(adr);
        setComment(adr, "Ord " + TypeOrdFloat(ord), 1);
        return;
    }

    if (Kind == tkString)
    {
        ord = ReMakeByte(adr);
        setComment(adr, "MaxLength", 1);
        return;
    }

    if (Kind == tkClass)
    {
        ord = MakeOffset(adr);

        if (ord)
        {
            ord = ord - 0x4c; // makes offset to RTTI from ptr to VTBL
            setComment(adr, "Class: " + getRTTIName(ord), 1);
        }

        adr = adr + 4;

        ord = MakeOffset(adr);
        setComment(adr, "Ptr to parent TypeInfo", 1);
        adr = adr + 4;
        ReMakeWord(adr);
        adr = adr + 2;
        return;
    }

    if (Kind == tkMethod)
    {
        Kind = ReMakeByte(adr);
        setComment(adr, TypeOrdMethod(Kind), 1);
        adr = adr + 1;
        max_value = ReMakeByte(adr);
        setComment(adr, "Count of args: " + ltoa(max_value, 10), 1);
        adr = adr + 1;

        for (count = 0; count < max_value; count++)
        {
            ord = ReMakeByte(adr);
            setComment(adr, "0x" + ltoa(count, 0x10) + " " + doParamFlag(ord), 1);
            adr = adr + 1;
            adr = adr + MakePStr(adr);
            adr = adr + MakePStr(adr);
        }

        /* for functions we have also return type ? */
        return;
    }

    if (Kind == tkInterface)
    {
        return;
    }

    if (Kind == tkInt64)
    {
        ReMakeQword(adr);
        setComment(adr, "MinInt64Value", 1);
        adr = adr + 8;
        ReMakeQword(adr);
        setComment(adr, "MaxInt64Value", 1);
        adr = adr + 8;
        return;
    }
}

#define RPKindGet       "Get"
#define RPKindSet       "Set"
#define RPKindStore     "Store"
#define RPKindIndex     "Index"

// hm, it`s not very clean function - just trust me...
static separatePointer(base_adr, adr, kind, propname)
{
    auto val;

    val = ReMakeInt(adr);

    if (0x80000000 == val)
    {
        setComment(adr, kind + ": none", 1);
        return;
    }

    if (0xff000000 == (0xff000000 & val)) // field index
    {
        val = val & 0xffff;
        setComment(adr, kind + ": offset 0x" + ltoa(val, 0x10), 1);
        return;
    }

    if (0xfe000000 == (0xfe000000 & val)) // virtual function
    {
        val = val & 0xffff;
        val = Dword(base_adr + 76 + val); // ptr in VTBL
        setComment(adr, kind + ": virtual method at " + ltoa(val, 0x10), 1);
        return;
    }

    if (RPKindStore == kind && 1 == val)
    {
        setComment(adr, kind + " always", 1);
        return;
    }

    // otherwise - function
    //MakeNameFOffset(adr,kind+propname);
    MakeNameFOffset(adr, "@" + getRTTIName(base_adr) + "@" + kind + propname);

    setComment(adr, kind + " function", 1);
}

// process published properties
// returns pointer to next parent`s struct
// Params:
//  adr      - address of TypeInfo
//  base_adr - address of RTTI
static doPublished(adr, base_adr)
{
    auto str_len, count, val;
    auto res, pname;
    auto pp_count; // count of published properties

    res = 0;
    // 1st byte - unknown
    ReMakeByte(adr);
    adr = adr + 1;
    // next - Pascal string - name of class
    adr = adr + MakePStr(adr);
    // VTBL pointer
    MakeOffset(adr);
    adr = adr + 4;
    // next - pointer to pointer to next this struct :-)
    str_len = MakeOffset(adr);

    if (str_len != 0)
    {
        res = MakeOffset(str_len);
    }

    adr = adr + 4;

    // WORD - unknown
    ReMakeWord(adr);

    adr = adr + 2;

    // next - name of Unit name
    adr = adr + MakePStr(adr);

    /*
      process published properties TypeInfo
      TPropInfo=packed record
        PropType:PPTypeInfo;
        GetProc:Pointer;
        SetProc:Pointer;
        StoredProc:Pointer;
        Index:Integer;
        Default:Longint;
        NameIndex:SmallInt;
        Name:ShortString;
      end;
    */
    pp_count = ReMakeWord(adr);

    setComment(adr, ltoa(pp_count, 10) + " published properties", 1);

    adr = adr + 2;

    for (count = 0; count < pp_count; count++)
    {
        // PropType
        val = MakeOffset(adr);

        if (val)
        {
            processTypeInfo(val);
            setComment(adr, get_type_name(val), 1);
        }

        // PropName
        pname = GetPStr(adr + 0x1A);

        if (!check_name(pname))
        {
            pname = "";
        }

        adr = adr + 4;

        // GetProc
        separatePointer(base_adr, adr, RPKindGet, pname);

        adr = adr + 4;

        // Set Proc
        separatePointer(base_adr, adr, RPKindSet, pname);

        adr = adr + 4;

        // StoredProc
        separatePointer(base_adr, adr, RPKindStore, pname);

        adr = adr + 4;

        // Index - ??
        separatePointer(base_adr, adr, RPKindIndex, pname);

        adr = adr + 4;

        // Default
        val = ReMakeInt(adr);

        if (0x80000000 == val)
        {
            setComment(adr, "nodefault", 1);
        }
        else
        {
            setComment(adr, "default value", 1);
        }

        adr = adr + 4;

        // NameIndex - ?
        val = ReMakeWord(adr);

        setComment(adr, "NameIndex 0x" + ltoa(val, 0x10), 1);

        adr = adr + 2;

        // Name
        adr = adr + MakePStr(adr);
    }

    return res;
}

// process dynamic methods table
static processDynamic(adr, prefix)
{
    auto count, base, i, d;
    count = ReMakeWord(adr);
    setComment(adr, ltoa(count, 10) + " dynamic method(s)", 0);
    adr = adr + 2;
    base = adr + 2 * count;

    for (i = 0; i < count; i++)
    {
        d = ReMakeWord(adr);
        MakeNameFOffset(base + 4 * i, prefix + "Dyn" + ltox(d));
        setComment(base + 4 * i, "Dynamic 0x" + ltoa(d, 0x10), 1);
        adr = adr + 2;
    }

    return count;
}

// makes tricky VTBL entries
static makeF2Offset(adr, name)
{
    auto ref_adr, fname;
    ref_adr = MakeOffset(adr);

    if (ref_adr != 0)
    {
        add_dref(adr, ref_adr, 0);
    }

    if (ref_adr < adr)
    {
        setComment(adr, name, 0);
        //MakeComm(adr,name);
    }
    else
    {
        fname = GetFunctionName(ref_adr);
        if ((fname == "") || (fname == "sub_" + ltox(ref_adr)) || (substr(fname, 0, 16) == "unknown_libname_") ||
                (substr(fname, 0, 8) == "nullsub_"))
        {
            //ReMakeFunc(ref_adr);
            MakeName(ref_adr, name);
        }
    }

    return ref_adr;
}

// makes TGUID
static make_TGUID(adr)
{
    auto count;
    // first DWORD
    ReMakeInt(adr);
    adr = adr + 4;
    // next - two WORD`s
    ReMakeWord(adr);
    ReMakeWord(adr + 2);
    adr = adr + 4;

    for (count = 0; count < 8; count++)
    {
        ReMakeByte(adr++);
    }
}

static padZeros(str, desired_len)
{
    auto len;
    len = strlen(str);
    len = desired_len - len;

    while (len)
    {
        str = "0" + str;
        len--;
    }

    return str;
}

static getTGUIDstr(adr)
{
    auto res, count;

    count = Dword(adr);
    res = "{" + padZeros(ltoa(count, 0x10), 8);
    adr = adr + 4;
    count = Word(adr);
    res = res + "-" + padZeros(ltoa(count, 0x10), 4);
    adr = adr + 2;
    count = Word(adr);
    res = res + "-" + padZeros(ltoa(count, 0x10), 4);
    adr = adr + 2;
    count = Byte(adr++);
    res = res + "-" + padZeros(ltoa(count, 0x10), 2);
    count = Byte(adr++);
    res = res + padZeros(ltoa(count, 0x10), 2) + "-";

    for (count = 0; count < 6; count++)
    {
        res = res + padZeros(ltoa(Byte(adr + count), 0x10), 2);
    }

    return res + "}";
}

// process interfaces table
static processIntf(adr)
{
    auto count;

    count = ReMakeInt(adr);
    setComment(adr, "Count of interfaces " + ltoa(count, 10), 1);
    adr = adr + 4;

    for (; count; count--)
    {
        // TGUID
        make_TGUID(adr);
        setComment(adr, getTGUIDstr(adr), 1);
        adr = adr + 0x10;
        // next - pointer to VTBL
        MakeOffset(adr);
        setComment(adr, "pointer to VTBL", 0);
        adr = adr + 4;
        // next - IOffset
        setComment(adr, "Offset " + ltoa(MakeOffset(adr), 0x10), 1);
        adr = adr + 4;
        // next - ImplGetter
        ReMakeInt(adr);
        adr = adr + 4;
    }
}

// returns count of dynamic methods
// Params:
//  adr - address of RTTI
static get_dyncount(adr)
{
    auto deref;
    deref = Dword(adr + 0x1c);
    if (!deref)
    {
        return 0;
    }

    return Word(deref);
}

// Process whole RTTI structure - work horse
static _processRTTI(adr, is_recursive)
{
    auto count, saveadr, rtti_base;
    auto res, ref_adr;
    auto my_name, rtti_name;
    auto vtbl_adr, vtbl_end, init_adr;

    my_name = getRTTIName(adr);
    vtbl_end = 0;

    /*
    { Virtual method table entries }

    0x00  vmtSelfPtr           = -76;
    0x04  vmtIntfTable         = -72;
    0x08  vmtAutoTable         = -68;
    0x0C  vmtInitTable         = -64;
    0x10  vmtTypeInfo          = -60;
    0x14  vmtFieldTable        = -56;
    0x18  vmtMethodTable       = -52;
    0x1C  vmtDynamicTable      = -48;
    0x20  vmtClassName         = -44;
    0x24  vmtInstanceSize      = -40;
    0x28  vmtParent            = -36;
    vmtSafeCallException = -32 deprecated;  // don't use these constants.
    vmtAfterConstruction = -28 deprecated;  // use VMTOFFSET in asm code instead
    vmtBeforeDestruction = -24 deprecated;
    vmtDispatch          = -20 deprecated;
    vmtDefaultHandler    = -16 deprecated;
    vmtNewInstance       = -12 deprecated;
    vmtFreeInstance      = -8 deprecated;
    vmtDestroy           = -4 deprecated;
    */

    // 0x28 - vmtParent - pointer to parent`s RTTI struct
    // process parent if it is not already processed
    res = MakeOffset(adr + 0x28);
    if (is_recursive && res && Name(res) != getRTTIName(res))
    {
        _processRTTI(res, is_recursive);
    }

    // 0x00 - vmtSelfPtr - VTBL pointer
    vtbl_adr = MakeOffset(adr);

    // 0x04 - vmtIntfTable
    res = MakeOffset(adr + 4);
    if (res)
    {
        processIntf(res);
        setComment(adr + 4, "Interfaces table", 1);
    }

    // 0x08 - vmtAutoTable
    res = MakeOffset(adr + 8);
    if (res)
    {
        setComment(adr + 8, "Auto table", 1);
    }

    // 0x0C - vmtInitTable
    init_adr = MakeOffset(adr + 0x0C); // offset to init's
    if (init_adr != 0 && Byte(init_adr) == 0x0E)
    {
        setComment(adr + 0x0C, "Init table", 1);
        MakeName(init_adr, "@" + my_name + "@Init");
        ReMakeWord(init_adr);
        ReMakeInt(init_adr + 2);
        count = ReMakeInt(init_adr + 6);
        setComment(init_adr + 6, ltoa(count, 10) + " entries", 1);
        init_adr = init_adr + 0x0A;

        for (; count != 0; count--)
        {
            MakeOffset(init_adr);
            ReMakeInt(init_adr + 4);
            init_adr = init_adr + 8;
        }
    }

    // 0x10 - vmtTypeInfo - list of parents
    count = MakeOffset(adr + 0x10);
    if (count != 0) // also process first parent for this class
    {
        MakeUnkn(count, 0);
        MakeName(count, "@" + my_name + "@Published");
        doPublished(count, adr);
        setComment(adr + 0x10, "Type info (Published properties)", 1);

        if (Dword(count - 4) == count)
        {
            MakeOffset(count - 4);
            MakeName(count - 4, "@" + my_name + "@TypeInfo");
        }
    }

    // 0x14 - vmtFieldTable - owned components
    count = MakeOffset(adr + 0x14);
    if (count != 0)
    {
        setComment(adr + 0x14, "Field table (Subcomponents)", 1);
        rtti_base = processOwned(count);

        if (is_recursive)
        {
            count = getOwnedCount(count);

            for (; count != 0; count--)
            {
                //if (Dword(rtti_base) && Name(Dword(rtti_base))!=getRTTIName(Dword(rtti_base)))
                //added check for valid rtti names (english letters, numbers, '_', '~')
                if (Dword(rtti_base))
                {
                    rtti_name = getRTTIName(Dword(rtti_base));

                    if (Name(Dword(rtti_base)) != rtti_name)
                    {
                        if ((substr(rtti_name, 0, 1) >= "A") && (substr(rtti_name, 0, 1) <= "~"))
                        {
                            _processRTTI(Dword(rtti_base), is_recursive);
                        }
                    }
                }

                rtti_base = rtti_base + 4;
            }
        }
    }

    // 0x18 - vmtMethodTable - event handlers list
    count = MakeOffset(adr + 0x18);

    if (count != 0)
    {
        MakeName(count, "@" + my_name + "@EvHandlers");
        processHandlers(count, getRTTIName(adr));
        setComment(adr + 0x18, "Method table (Event handlers)", 1);
    }

    // 0x1C - vmtDynamicTable - pointer to dynamic functions list
    count = MakeOffset(adr + 0x1C);

    if (count != 0)
    {
        saveadr = count;
        count = processDynamic(count, "@" + my_name + "@");
        setComment(adr + 0x1C, ltoa(count, 10) + " dynamic method(s)", 1);
        MakeName(saveadr, "@" + my_name + "@DynTable");
    }

    // 0x20 - vmtClassName - pointer to class name
    count = MakeOffset(adr + 0x20);

    if (count != 0)
    {
        MakePStr(count);
        my_name = GetPStr(count);
        setComment(adr + 0x20, "Class name: " + my_name, 1);
        MakeName(adr, my_name);
        MakeName(count, "@" + my_name + "@Name");
    }

    // 0x24 - vmtInstanceSize - size of class
    ReMakeInt(adr + 0x24);

    setComment(adr + 0x24, "Instance size", 1);

    // 0x28 - vmtParent - pointer to parent`s RTTI struct
    res = MakeOffset(adr + 0x28);

    setComment(adr + 0x28, "Parent", 1);

    // 0x2C SafeCallException
    makeF2Offset(adr + 0x2C, "@" + my_name + "@SafeCallException");

    // 0x30 AfterConstruction
    makeF2Offset(adr + 0x30, "@" + my_name + "@AfterConstruction");

    // 0x34 BeforeDestruction
    makeF2Offset(adr + 0x34, "@" + my_name + "@BeforeDestruction");

    // 0x38 Dispatch
    makeF2Offset(adr + 0x38, "@" + my_name + "@Dispatch");

    // 0x3C DefaultHandler
    makeF2Offset(adr + 0x3C, "@" + my_name + "@DefaultHandler");

    // 0x40 NewInstance
    makeF2Offset(adr + 0x40, "@" + my_name + "@NewInstance");

    // 0x44 FreeInstance
    makeF2Offset(adr + 0x44, "@" + my_name + "@FreeInstance");

    // 0x48 Destroy
    makeF2Offset(adr + 0x48, "@" + my_name + "@Destroy");

    // now try to process VTBL
    if (vtbl_adr)
    {
        MakeName(vtbl_adr, "@" + my_name + "@VTBL");

        for (count = 4; count < 40; count++)
        {
            saveadr = Dword(adr + count);

            if (saveadr >= vtbl_adr)
            {
                if (!vtbl_end)
                {
                    vtbl_end = saveadr;
                }
                else if (vtbl_end > saveadr)
                {
                    vtbl_end = saveadr;
                }
            }
        }

        /* for debug only: Message("end of VTBL is "+ltoa(vtbl_end,0x10)); */
        saveadr = vtbl_adr;

        for (count = vtbl_end - vtbl_adr; count > 0; count = count - 4)
        {
            //MakeFOffset(vtbl_adr);
            //ref_adr=MakeFOffset(vtbl_adr);
            MakeNameFOffset(vtbl_adr, "@" + my_name + "@Fn" + ltox(vtbl_adr - saveadr));
            vtbl_adr = vtbl_adr + 4;
        }
    }

    //already done at the beginning
    //if (is_recursive && res) _processRTTI(res,is_recursive);

    // return res;

    if (vtbl_end)
    {
        return vtbl_end;
    }

    return adr + 0x4c;
}

// pre-check RTTI and ask about it
static askRTTI(adr)
{
    auto name, name1, dyn_count, what_say;
    name = getRTTIName(adr);
    name1 = substr(name, 0, 1);
    //Message(":%s:\n",name1);

    if ((name1 < "A") || (name1 > "~"))
    {
        Message("Bad RTTI name: %s\n", name);
        return 0;
    }

    dyn_count = get_dyncount(adr);

    what_say = "Process RTTI of class \"" + name + "\"";

    if (dyn_count)
    {
        what_say = what_say + " with " + ltoa(dyn_count, 10) + " dynamic method(s)";
    }

    what_say = what_say + "?";

    return AskYN(1, what_say);
}

// main function - process RTTI with small pre-checking
static processRTTI(adr)
{
    if (1 == askRTTI(adr))
    {
        _processRTTI(adr, 0);
    }
}

// process all RTTI from this (recursive)
static allRTTI(adr)
{
    if (1 == askRTTI(adr))
    {
        _processRTTI(adr, 1);
    }
}

static UnitEntries(adr)
{
    auto count, i, ref_adr, fname;

    ref_adr = adr;
    count = ReMakeInt(adr);
    adr = MakeOffset(adr + 4);

    if (!adr || !count)
    {
        return;
    }

    MakeName(ref_adr, "InitTable");

    for (i = 0; i < count; i++)
    {
        /*
        ref_adr = MakeOffset(adr);
        ReMakeFunc(ref_adr);
        MakeName(ref_adr, "@Unit" + ltoa(i + 1, 10) + "@Initialization");
        setComment(adr, "Unit " + ltoa(i + 1, 10) + " Initialization", 1);
        adr = adr + 4;
        ref_adr=MakeOffset(adr);
        ReMakeFunc(ref_adr);
        MakeName(ref_adr, "@Unit" + ltoa(i + 1, 10)+"@Finalization");
        adr = adr + 4;
        */
        MakeNameFOffset(adr, "@Unit" + ltoa(i + 1, 10) + "@Initialization");
        MakeNameFOffset(adr + 4, "@Unit" + ltoa(i + 1, 10) + "@Finalization");
        setComment(adr, "Unit" + ltoa(i + 1, 10) + ".Initialization", 1);
        setComment(adr + 4, "Unit" + ltoa(i + 1, 10) + ".Finalization", 1);
        MakeComm(adr, "Unit " + ltoa(i + 1, 10));
        adr = adr + 8;
    }
}

static all_RTTI(adr)
{
    auto count, seg, s_adr, e_adr, val;

    count = 0;

    for (seg = FirstSeg(); seg != BADADDR; seg = NextSeg(seg))
    {
        s_adr = SegStart(seg);
        e_adr = SegEnd(seg);

        if (adr >= s_adr && adr <= e_adr)
        {
            count = 1;
            break;
        }
    }

    if (!count)
    {
        Message("Cannot find segment for address " + ltoa(adr, 0x10) + "\n");
        return;
    }

    for (count = s_adr; count < e_adr;)
    {
        val = Dword(count);
        if (val != (count + 0x4C))
        {
            count = count + 4;
            continue;
        }

        if (!check_pname(count + 0x20))
        {
            count = count + 4;
            continue;
        }

        count = _processRTTI(count, 0);
    }
}

// 1st main function - recursively process RTTI at current EA
static RProcessRTTI()
{
    auto adr, oldstatus;

    adr = ScreenEA();
    if (1 == askRTTI(adr))
    {
        oldstatus = SetStatus(IDA_STATUS_THINKING);
        _processRTTI(adr, 1);
        SetStatus(oldstatus);
    }
}

// 2nd main function - process RTTI at current EA
static ProcessThisRTTI()
{
    auto adr, oldstatus;

    adr = ScreenEA();
    if (1 == askRTTI(adr))
    {
        oldstatus = SetStatus(IDA_STATUS_THINKING);
        _processRTTI(adr, 0);
        SetStatus(oldstatus);
    }
}

static InitTable()
{
    auto adr, d;

    adr = ScreenEA();
    d = Dword(adr);
    if (d > 0xFFFF)
    {
        Message("Too big unit count: " + ltoa(d, 10) + " - maybe incorrect address?\n");
        return;
    }

    d = Dword(adr + 4);
    if (d != adr + 8)
    {
        Message("InitTableEntries is " + ltox(d) + ", should be " + ltox(adr + 8) + "\n");
        return;
    }

    d = AskYN(1, "Process InitTable at " + ltox(adr) + "?");
    if (d == 1)
    {
        //Message("Calling UnitEntries(address)...\n");
        UnitEntries(adr);
    }
}

static main()
{
    auto hkey, res, reason;

    Message("Call UnitEntries(address) to process dcu entries at start point\n");

    hkey = "Alt-F";
    res = AddHotkey(hkey, "InitTable");
    if (res == -1)
    {
        reason = "bad argument(s)";
    }
    else if (res == -2)
    {
        reason = "bad hotkey name";
    }
    else if (res == -3)
    {
        reason = "too many IDC hotkeys";
    }
    else if (res == 0)
    {
        Message("Press " + hkey + " to process dcu entries at current address\n");
    }
    else
    {
        Warning("Cannot assign hotkey " + hkey + " to UnitEntriesEA script. Reason: " + reason);
    }

    hkey = "Alt-E";
    res = AddHotkey(hkey, "ProcessThisRTTI");
    if (res == -1)
    {
        reason = "bad argument(s)";
    }
    else if (res == -2)
    {
        reason = "bad hotkey name";
    }
    else if (res == -3)
    {
        reason = "too many IDC hotkeys";
    }
    else if (res == 0)
    {
        Message("Press " + hkey + " to process RTTI at current address\n");
    }
    else
    {
        Warning("Cannot assign hotkey " + hkey + " to ProcessThisRTTI script. Reason: " + reason);
    }

    hkey = "Alt-W";
    res = AddHotkey(hkey, "RProcessRTTI");
    if (res == -1)
    {
        reason = "bad argument(s)";
    }
    else if (res == -2)
    {
        reason = "bad hotkey name";
    }
    else if (res == -3)
    {
        reason = "too many IDC hotkeys";
    }
    else if (res == 0)
    {
        Message("Press " + hkey + " to recursively process RTTI from this address\n");
    }
    else
    {
        Warning("Cannot assign hotkey " + hkey + " to RProcessRTTI script. Reason: " + reason);
    }
}
