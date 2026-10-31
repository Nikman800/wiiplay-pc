#include <Pack/RPSystem.h>
// Ported from doldecomp/ogws (CC0) src/Pack/RPSystem/RPSysTagParms.cpp @ f5a1bb5e; Wii Play changes marked "Wii Play:".

/******************************************************************************
 *
 * RPSysTagParm
 *
 ******************************************************************************/

/**
 * @brief Constructor
 *
 * @param pParent Parent container
 * @param pTag Parameter tag
 */
RPSysTagParm::RPSysTagParm(RPSysTagParameters* pParent, TagType pTag)
    : mpTag(pTag), mpNext(NULL) {

    pParent->add(this);
}

/**
 * @brief Gets the parameter's tag name
 */
// Wii Play: no out-of-line copy in the binary (inlined; unused copy dead-stripped)
inline TagType RPSysTagParm::getTag() {
    return mpTag;
}

/**
 * @brief Accesses the next parameter
 */
// Wii Play: no out-of-line copy in the binary (inlined; unused copy dead-stripped)
inline RPSysTagParm* RPSysTagParm::getNext() {
    return mpNext;
}
/**
 * @brief Sets the next parameter
 *
 * @param pNext Next parameter
 */
// Wii Play: no out-of-line copy in the binary (inlined; unused copy dead-stripped)
inline void RPSysTagParm::setNext(RPSysTagParm* pNext) {
    mpNext = pNext;
}

/******************************************************************************
 *
 * RPSysPrimTagParm<int>
 *
 ******************************************************************************/

/**
 * @brief Reads the parameter's value
 *
 * @param rStream Input stream
 */
template <> void RPSysPrimTagParm<int>::read(EGG::Stream& rStream) {
    mValue = rStream.read_s32();
}

/**
 * @brief Writes the parameter's value
 *
 * @param rStream Output stream
 */
template <> void RPSysPrimTagParm<int>::write(EGG::Stream& rStream) {
    rStream.write_s32(mValue);
}

/**
 * @brief Prints the parameter's value
 */
template <> void RPSysPrimTagParm<int>::dump() {}

/******************************************************************************
 *
 * RPSysStringTagParm
 *
 ******************************************************************************/

/**
 * @brief Constructor
 *
 * @param pParent Parent container
 * @param pTag Parameter tag
 */
RPSysStringTagParm::RPSysStringTagParm(RPSysTagParameters* pParent,
                                       TagType pTag)
    : RPSysTagParm(pParent, pTag), mpValue(NULL) {}

/**
 * @brief Reads the parameter's value
 *
 * @param rStream Input stream
 */
void RPSysStringTagParm::read(EGG::Stream& rStream) {
    // NONMATCHING-DEBT: EGG::Stream::readString is declared (EGG reference) to return const
    // char*, while TagType is char* (the mangled RPSysTagParm constructors take Pc).
    mpValue = const_cast<TagType>(rStream.readString(NULL, 0));
}

/**
 * @brief Writes the parameter's value
 *
 * @param rStream Output stream
 */
void RPSysStringTagParm::write(EGG::Stream& rStream) {
    rStream.writeString(const_cast<char*>(mpValue));
}

/**
 * @brief Prints the parameter's value
 */
void RPSysStringTagParm::dump() {}

/******************************************************************************
 *
 * RPSysTagParameters
 *
 ******************************************************************************/

/**
 * @brief Constructor
 *
 * @param pName Parameter group name
 */
RPSysTagParameters::RPSysTagParameters(TagType pName)
    : mpName(pName), mpParameters(NULL) {}

/**
 * @brief Appends a new child parameter to this group
 *
 * @param pChild Child parameter
 */
// Wii Play: no out-of-line copy in the binary (inlined; unused copy dead-stripped)
inline void RPSysTagParameters::add(RPSysTagParm* pChild) {
    if (mpParameters != NULL) {
        pChild->setNext(mpParameters);
        mpParameters = pChild;
    } else {
        mpParameters = pChild;
    }
}

// Wii Play: out of line, defined last (Wii Sports has them inline in the header)
void RPSysTagParm::read(EGG::Stream& /* rStream */) {}

void RPSysTagParm::write(EGG::Stream& /* rStream */) {}

void RPSysTagParm::dump() {}
