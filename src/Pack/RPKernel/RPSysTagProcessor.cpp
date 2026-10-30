#include <Pack/RPKernel/RPSysTagProcessor.h>

#include <Pack/RPKernel/RPSysSystemWinMgr.h>

#include <egg/util.h>

#include <nw4r/ut.h>

#include <cstring>
#include <cwchar>

RPSysTagProcessor* RPSysTagProcessor::spInstance = NULL;

static nw4r::ut::Color sTagColors[] = {
    nw4r::ut::Color::BLACK,
    nw4r::ut::Color::RED,
    nw4r::ut::Color::GREEN,
    nw4r::ut::Color::BLUE,
};

nw4r::ut::Color GetTextColor(const nw4r::ut::CharWriter* pWriter);

// Wii Play: the shared tag handler behind Process and CalcRect, and the ruby (furigana) tag. Both
// take the processor as their first argument; they are free functions here so that the shared
// header keeps its declarations.
RPSysTagProcessor::Operation
RPSysTagProcessor_ProcessCommon(RPSysTagProcessor* pThis, nw4r::ut::Rect* pRect, u16 ch,
                                nw4r::ut::PrintContext<wchar_t>* pCtx);
void RPSysTagProcessor_ProcessRuby(RPSysTagProcessor* pThis, nw4r::ut::Rect* pRect,
                                   nw4r::ut::PrintContext<wchar_t>* pCtx,
                                   u8 tagLength, u8* pParam);

inline GXColor MakeTagColor(const nw4r::ut::Color& rTag,
                            const nw4r::ut::CharWriter* pWriter) {
    GXColor color;
    color.r = rTag.r;
    color.g = rTag.g;
    color.b = rTag.b;
    color.a = GetTextColor(pWriter).a;
    return color;
}

inline RPSysTagProcessor::RPSysTagProcessor() {}

void RPSysTagProcessor::CreateInstance() {
    spInstance = new RPSysTagProcessor();
}

RPSysTagProcessor::~RPSysTagProcessor() {}

void RPSysTagProcessor::PreProcessEx(const wchar_t* pSrc, wchar_t* pDst,
                                     u32 dstLen, int* pOutLen, int argc,
                                     va_list args) {
    const wchar_t* pRead = pSrc;
    wchar_t* pWrite = pDst;
    int argi = 0;

    do {
        wchar_t c = *pRead;

        if (c == L'\0') {
            *pWrite = L'\0';
            goto end;
        }

        if (c == 0x1A) {
            u8 tagLength = 0;
            u32 tagId = 0;
            void* pParam = NULL;
            EGG::MsgRes::analyzeTag(c, pRead + 1, &tagLength, &tagId, &pParam);

            if ((tagId & 0x00FF0000) == 0) {
                if (argc > argi) {
                    const wchar_t* pArg = va_arg(args, const wchar_t*);
                    std::memcpy(pWrite, pArg, std::wcslen(pArg) * sizeof(wchar_t));
                    argi++;
                    pWrite += std::wcslen(pArg);
                }
            } else if (tagId == 0x20000) {
                u16 v = *static_cast<u16*>(pParam);
                RPSysSystemWinMgr::instance()->mCloseDelay = v;
            } else if (tagId == 0x20001) {
                u8 v = *static_cast<u8*>(pParam);
                RPSysSystemWinMgr* pMgr = RPSysSystemWinMgr::instance();
                pMgr->mNextButton = v;
                pMgr->mSelButton = v;
                pMgr->mHoverButton[0] = v;
                pMgr->mHoverButton[1] = v;
                pMgr->mHoverButton[2] = v;
                pMgr->mHoverButton[3] = v;
            } else if (tagId == 0x20002) {
                // NONMATCHING-DEBT: RPSysSystemWinMgr::mSeType[index] as an address sum. The natural
                // RPSysSystemWinMgr::instance()->mSeType[*static_cast<u8*>(pParam)] = true (also via
                // a local manager pointer, a local index, pointer arithmetic) swaps r0/r4 for the
                // instance and the index (3 words in PreProcessEx).
                *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(RPSysSystemWinMgr::instance()) +
                                         *static_cast<u8*>(pParam) +
                                         offsetof(RPSysSystemWinMgr, mSeType)) = true;
            } else if (tagId == 0x20003) {
                u8 page = *static_cast<u8*>(pParam);
                RPSysSystemWinMgr::instance()->setMessagePage(page);
            } else {
                std::memcpy(pWrite, pRead, tagLength + sizeof(wchar_t));
                pWrite += tagLength / sizeof(wchar_t) + 1;
            }

            pRead += tagLength / sizeof(wchar_t) + 1;
        } else {
            *pWrite = c;
            pRead++;
            pWrite++;
        }
    } while (dstLen > pWrite - pDst);

    pDst[dstLen - 1] = L'\0';

end:
    if (pOutLen != NULL) {
        *pOutLen = pWrite - pDst;
    }
}

RPSysTagProcessor::Operation
RPSysTagProcessor::Process(u16 ch, nw4r::ut::PrintContext<wchar_t>* pCtx) {
    return RPSysTagProcessor_ProcessCommon(this, NULL, ch, pCtx);
}

RPSysTagProcessor::Operation
RPSysTagProcessor::CalcRect(nw4r::ut::Rect* pRect, u16 ch,
                            nw4r::ut::PrintContext<wchar_t>* pCtx) {
    return RPSysTagProcessor_ProcessCommon(this, pRect, ch, pCtx);
}

RPSysTagProcessor::Operation
RPSysTagProcessor_ProcessCommon(RPSysTagProcessor* pThis, nw4r::ut::Rect* pRect, u16 ch,
                                nw4r::ut::PrintContext<wchar_t>* pCtx) {
    if (ch != 0x1A) {
        if (pRect != NULL) {
            return pThis->nw4r::ut::TagProcessorBase<wchar_t>::CalcRect(
                pRect, ch, pCtx);
        }
        return pThis->nw4r::ut::TagProcessorBase<wchar_t>::Process(ch, pCtx);
    }

    u8 tagLength = 0;
    u32 tagId = 0;
    void* pParam = NULL;
    EGG::MsgRes::analyzeTag(ch, pCtx->str, &tagLength, &tagId, &pParam);

    switch (tagId) {
    case 0x10000: {
        // NONMATCHING-DEBT: tagLength is a byte count; the natural str + tagLength / sizeof(wchar_t)
        // adds a shift/mask (10 words in ProcessCommon), so the byte offset stays.
        // The const_cast stays too: nw4r's PrintContext::str is const, and this tag really
        // upper-cases the next character in place.
        wchar_t* pNext = reinterpret_cast<wchar_t*>(
            reinterpret_cast<u8*>(const_cast<wchar_t*>(pCtx->str)) +
            tagLength);
        wchar_t next = *pNext;
        if (next >= L'a' && next <= L'z') {
            *pNext = next - 0x20;
        }
        break;
    }
    case 0xFF0000: {
        pCtx->writer->SetTextColor(
            MakeTagColor(sTagColors[*static_cast<u8*>(pParam)], pCtx->writer));
        break;
    }
    case 0xFF0001: {
        u16 scale = *static_cast<u16*>(pParam);
        if (scale == 0) {
            scale = 50;
        }
        f32 sx = scale / 100.0f;
        f32 sy = scale / 100.0f;
        pCtx->writer->SetScale(sx, sy);
        break;
    }
    case 0xFF0007: {
        RPSysTagProcessor_ProcessRuby(pThis, pRect, pCtx, tagLength,
                                      static_cast<u8*>(pParam));
        break;
    }
    }

    pCtx->str += tagLength / sizeof(wchar_t);
    return RPSysTagProcessor::OPERATION_DEFAULT;
}

nw4r::ut::Color GetTextColor(const nw4r::ut::CharWriter* pWriter) {
    return pWriter->GetTextColor();
}

inline void PrintRuby(nw4r::ut::TextWriterBase<wchar_t>& rubyWriter,
                     nw4r::ut::PrintContext<wchar_t>* pCtx,
                     nw4r::ut::Rect* pRect, u8 rubyLen, const wchar_t* pRuby,
                     f32 rubyWidth, f32 baseWidth,
                     const wchar_t* pBase, u8 baseLen) {
    f32 diff = baseWidth - rubyWidth;
    // Matching idiom: a const reference (not a plain f32) for its register numbering; the
    // original's exact form is unknown (docs/NONMATCHING.md, "Matching idioms").
    const f32& y = pCtx->writer->GetCursorY();
    f32 x = pCtx->writer->GetCursorX();
    if (0.0f < diff) {
        rubyWriter.SetCharSpace(diff / (rubyLen / 2));
        x += rubyWriter.GetCharSpace() / 2.0f;
    }
    rubyWriter.SetCursor(x, y - (pCtx->writer->GetFontAscent() + rubyWriter.GetFontDescent()));
    if (pRect == NULL) {
        rubyWriter.Print(pRuby, rubyLen);
    }

    u32 drawFlag = pCtx->writer->GetDrawFlag();
    pCtx->writer->SetDrawFlag(0x300);

    if (0.0f > diff) {
        pCtx->writer->MoveCursorX(-diff / 2.0f);
    }

    if (pRect == NULL) {
        pCtx->writer->Print(pBase, baseLen);
    }

    if (0.0f > diff) {
        pCtx->writer->MoveCursorX(-diff / 2.0f);
    }

    pCtx->writer->SetDrawFlag(drawFlag);

    if (pRect != NULL) {
        f32 width = nw4r::ut::Max(rubyWidth, baseWidth);

        if (!(pCtx->flags &
              nw4r::ut::PrintContext<wchar_t>::FLAGS_CHARSPACE)) {
            pRect->right = pRect->left + (width + pCtx->writer->GetCharSpace());
        } else {
            pRect->right = pRect->left + width;
        }

        pRect->bottom = pRect->top + pCtx->writer->GetFontHeight();
    }

    pCtx->str += baseLen;
}

// FUNCTIONAL (matching abandoned, user decision 2026-10-29): behaviour equals the original; the
// differences below are register allocation / instruction order only (docs/NONMATCHING.md).
// NONMATCHING (99.93%, 4 differences): y (f28 in the original) and the GetFontDescent result held
// across the GetFontAscent call (f29) are swapped (lfs 0x30, fmr, fadds, fsubs).
// 2026-10-29 (pairwise FPR replay, sim.py adapted: volatile f0-f13, callee-saved from f31 down;
// K = 32 and 33 both reproduce the trace): of all single and pairwise renumberings of the 50 live
// FPR vregs, the only fixes are y (f110) anywhere between diff (f50) and descent's fmr (f86),
// descent alone above y (> f110), or both moved with descent > y > diff (y just above a lowered
// diff, or y and descent both above f86); no pair of other values helps. Tried: a const-ref
// descent after y (merged into the fmr temp, f87, not promoted: 4), y read inline in the SetCursor
// expression (a low inline-return temp f39, descent f29 but diff/y/x shift: 16), y bound to
// rubyWriter.GetCursorY() (promoted to the top again, plus a stack load: 7).
// 2026-10-29 (b fixed, 9 -> 4): `pBase = pRuby; pBase += rubyLen;` gives the one extra PCode
// instruction ahead of the writer copy (a copy coalesced away later), so the copy's hidden block
// split falls before mFixedWidth as in the original; no extra instruction survives. Same effect:
// pBase = pRuby declared before baseLen with the += after it. No effect (still 7): the copy as
// `= *pCtx->writer`, through a `GetWriter(pCtx)` reference getter, from a `T* const` local, a
// GetRubyLen inline, `const u8 rubyLen`, (u32)rubyLen, pRuby/rubyLen order. Worse: a writer pointer
// local (13 to 113), a pStr parse pointer (pStr += rubyLen, or *p++ for baseLen), the locals
// before the CHARSPACE test, the copy before the locals.
// Part (a) after the fix (FPR trace: lvl-1 colours by descending vreg; needs descent > y > diff
// (f50) > x): the const-ref y is promoted to the top vreg (f110) above descent (f86). Binding y
// after `const f32& descent = rubyWriter.GetFontDescent()` gives descent f29 but y becomes the plain
// getter-load temp (f39, lowest): 10. y as a by-value or const-ref PrintRuby parameter (argument
// GetCursorY() or a ProcessRuby local y, after either CalcStringWidth), `y -= offset` forms (param
// or local, before diff), a named/const-ref descent or offset, diff as a by-value/const-ref
// parameter (with plain or const-ref y): 4 to 44, never better. Every GetCursorY load temp is
// numbered after diff (@7528) wherever y is written, and y's variable is merged into it.
// 2026-10-29 (inline-result pass): y from a GetBaseY(pCtx) accessor inline returning f32 (9) or
// const f32& (4, still promoted to the top vreg), descent through a GetRubyDescent inline (4: it
// stays the fmr of the call result), the offset or the whole `y - offset` from an inline (4 or 9),
// y passed straight into such an inline (14). An inline-result y is still the getter-load temp
// numbered after the named diff. diff as an inline result (CalcWidthDiff) after y gives the wanted
// order x < diff < y < descent (f40/f41/f43/f88) but diff, visited early, goes to the lvl-2 sweep
// (f31): 17; diff before y: 9 (diff above y). No form scored under 4.
// --sched (2026-10-29): part (b) is not a dependence or priority difference. The writer copy is
// split into two scheduling blocks with no branch between them (the front end has one flowgraph
// node there): codegen starts a new block once the block holds about 96 instructions at a member-
// copy boundary. Ours splits before mFont (rubyWriter+72), so lfs/stfs of mFixedWidth (+68) end
// the first block; the original splits one member earlier, so mFixedWidth's lfs shares the second
// block with li 0x300 / lfs 0.0f and takes f1. One more codegen instruction before the copy moves
// the split there: pBase computed as (pParam + 2 + rubyLen * 2) or the scale byte read before the
// copy reproduce the original block (b) exactly, but leave an extra add / an early lbz. The
// original had one more instruction ahead of the copy that does not survive to the object (an
// mr removed by coalescing, say). Not found: pBase as &pRuby[rubyLen], a writer reference local
// (before or after the locals), &pParam[2], local orders, rubyLen/baseLen types (u16, s16, u8,
// u32, int), (u8) or >> 1 forms, a rubySize local, a u8* pStr local, split declare/assign.
// Part (a) does not follow from (b): with the original block (b) y/descent are still f29/f28.
// --frontend (2026-10-28): y's const-ref binding temp is @7529, numbered between diff (@7530) and
// x (@7527) and kept by the front end (@7604 = &@7529, read as *@7604). Its slot would be vreg 51
// (diff 50, descent's fmr 86: the original's order), but the back end promotes the address-taken
// temp to a fresh top vreg (@7529'110), so y is coloured before descent. A plain y is propagated
// into GetCursorY's load temp (f42, below diff). diff as a const ref becomes a codegen temp (f82).
// No form puts y between f50 and f86: const f32 / direct-init y, y before diff, a named or const-ref
// descent/ascent/offset, x as const ref, diff as const ref with y plain or const-ref.
// 2026-10-28 (5): with pParam as u8*, y as `const f32&` bound to GetCursorY() inside PrintRuby
// fixed diff (f27) and its 5 uses (14 -> 9), in any position relative to diff and x. No effect on
// the y/descent swap: x/y/diff declaration orders, a named descent/ascent/offset, operand orders of
// the offset, SetCursorX/SetCursorY, y bound in ProcessRuby and passed by const ref, the widths or
// diff as const refs.
// Earlier: diff/y float registers (diff f27 / y f28 in the original; here diff was always
// coloured above y) and the scale-byte load (r3, after the writer setup stores, in the original;
// hoisted into r5 here). Matched: the integer registers need baseLen wider than u8 (a u8 local
// always takes r31) declared last, with the str advance done through PrintRuby's u8 parameter.
// No effect on diff/y: declaration and assignment orders of rubyWidth/baseWidth/diff/x/y inside
// or outside PrintRuby (all 720), PrintRuby parameter orders and reference parameters, const-ref
// bindings, a y -= form, splitting the cursor offset, a helper for the negative-diff moves.
// No effect on the scale load: *pParam, a pointer local, scale helpers (pointer or u8), setup
// statement orders, switch/!/ternary forms, any position of the four header locals.
// mwcc_trace (2026-10-28, --class fpr): the lvl-1 sweep colours descent, diff, y, x; diff is a named
// local of PrintRuby (early @ number) while y and x are the GetCursorY/GetCursorX load temps (later
// numbers, created whatever the source order). Without the diff local y and x get f28/f27 but diff is
// recomputed (49 differences); y as a PrintRuby local or param in any position is still a load temp.
// Const-ref pass (2026-10-28): y as `const f32&` (named local or GetCursorY() bound directly), the
// widths or diff as const refs, diff as a by-value/const-ref argument: the binding temp is created
// early (@7474) but the copy is coalesced into the getter-load temp (lower vreg wins), so y stays
// @7599. Getter loads are numbered after every inline's named locals and arithmetic temps, so y
// cannot get a vreg above diff's. diff as an inline return value (@7606, between y and x) has the
// right order but too many higher-numbered neighbours and moves to the lvl-2 sweep (f31).
// 2026-10-28 (4, FPR replay of the trace, K = 31..33 reproduces it): diff renumbered between x and y
// (f42) is still lvl-2 at K <= 32 (32 neighbours left when visited); y renumbered above diff (> f51)
// matches at K >= 32. So y has to be an inline-time value, not a getter-load temp. No effect: diff
// assigned in-expression at its first use (declared before or after x), y through a reference
// out-parameter helper (still propagated into the load temp), y read in PrintRuby after diff, a
// writer reference. Scale load: the prepass scheduler hoists it into the copy-constructor block
// (B4 of --pcode), so it interferes with the writer pointer; a setup helper, a scale helper (u8 or
// pointer) or one helper for both do not split the block.
void RPSysTagProcessor_ProcessRuby(RPSysTagProcessor* pThis, nw4r::ut::Rect* pRect,
                                   nw4r::ut::PrintContext<wchar_t>* pCtx,
                                   u8 tagLength, u8* pParam) {
    if (!(pCtx->flags & nw4r::ut::PrintContext<wchar_t>::FLAGS_CHARSPACE)) {
        pCtx->writer->MoveCursorX(pCtx->writer->GetCharSpace());
    }

    u8 rubyLen = (tagLength - 6) / sizeof(wchar_t);
    const wchar_t* pRuby = reinterpret_cast<const wchar_t*>(pParam + 2);
    // The base text follows the ruby text. Written in two steps: the original has one more
    // instruction ahead of the writer copy (it moves the copy's block split, see above).
    const wchar_t* pBase = pRuby;
    pBase += rubyLen;
    int baseLen = pParam[1];

    nw4r::ut::TextWriterBase<wchar_t> rubyWriter(*pCtx->writer);
    rubyWriter.SetDrawFlag(0x300);
    rubyWriter.SetCharSpace(0.0f);
    rubyWriter.SetLineSpace(0.0f);

    if (pParam[0] == 0) {
        rubyWriter.SetScale(0.5f * rubyWriter.GetScaleH(),
                            0.5f * rubyWriter.GetScaleV());
    } else {
        rubyWriter.SetScale(pParam[0] * rubyWriter.GetScaleH() / 100.0f,
                            pParam[0] * rubyWriter.GetScaleV() / 100.0f);
    }

    f32 rubyWidth = rubyWriter.CalcStringWidth(pRuby, rubyLen);
    f32 baseWidth = pCtx->writer->CalcStringWidth(pBase, baseLen);
    PrintRuby(rubyWriter, pCtx, pRect, rubyLen, pRuby,
              rubyWidth, baseWidth, pBase, baseLen);
}
