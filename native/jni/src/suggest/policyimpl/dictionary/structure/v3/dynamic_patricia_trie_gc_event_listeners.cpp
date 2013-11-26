/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "suggest/policyimpl/dictionary/structure/v3/dynamic_patricia_trie_gc_event_listeners.h"

#include "suggest/core/policy/dictionary_header_structure_policy.h"
#include "suggest/policyimpl/dictionary/structure/pt_common/pt_node_params.h"
#include "suggest/policyimpl/dictionary/structure/pt_common/pt_node_writer.h"
#include "suggest/policyimpl/dictionary/structure/v3/dynamic_patricia_trie_writing_utils.h"
#include "suggest/policyimpl/dictionary/utils/forgetting_curve_utils.h"

namespace latinime {

bool DynamicPatriciaTrieGcEventListeners
        ::TraversePolicyToUpdateUnigramProbabilityAndMarkUselessPtNodesAsDeleted
                ::onVisitingPtNode(const PtNodeParams *const ptNodeParams) {
    // PtNode is useless when the PtNode is not a terminal and doesn't have any not useless
    // children.
    bool isUselessPtNode = !ptNodeParams->isTerminal();
    if (ptNodeParams->isTerminal() && mIsDecayingDict) {
        const int newProbability =
                ForgettingCurveUtils::getEncodedProbabilityToSave(ptNodeParams->getProbability(),
                        mHeaderPolicy);
        // Update probability.
        if (!mPtNodeWriter->updatePtNodeProbability(ptNodeParams, newProbability)) {
            return false;
        }
        if (!ForgettingCurveUtils::isValidEncodedProbability(newProbability)) {
            isUselessPtNode = true;
        }
    }
    if (mChildrenValue > 0) {
        isUselessPtNode = false;
    } else if (ptNodeParams->isTerminal()) {
        // Remove children as all children are useless.
        if (!mPtNodeWriter->updateChildrenPosition(ptNodeParams,
                NOT_A_DICT_POS /* newChildrenPosition */)) {
            return false;
        }
    }
    if (isUselessPtNode) {
        // Current PtNode is no longer needed. Mark it as deleted.
        if (!mPtNodeWriter->markPtNodeAsDeleted(ptNodeParams)) {
            return false;
        }
    } else {
        mValueStack.back() += 1;
        if (ptNodeParams->isTerminal()) {
            mValidUnigramCount += 1;
        }
    }
    return true;
}

bool DynamicPatriciaTrieGcEventListeners::TraversePolicyToUpdateBigramProbability
        ::onVisitingPtNode(const PtNodeParams *const ptNodeParams) {
    if (!ptNodeParams->isDeleted() && ptNodeParams->hasBigrams()) {
        int bigramEntryCount = 0;
        if (!mPtNodeWriter->updateAllBigramEntriesAndDeleteUselessEntries(ptNodeParams,
                &bigramEntryCount)) {
            return false;
        }
        mValidBigramEntryCount += bigramEntryCount;
    }
    return true;
}

// Writes dummy PtNode array size when the head of PtNode array is read.
bool DynamicPatriciaTrieGcEventListeners::TraversePolicyToPlaceAndWriteValidPtNodesToBuffer
        ::onDescend(const int ptNodeArrayPos) {
    mValidPtNodeCount = 0;
    int writingPos = mBufferToWrite->getTailPosition();
    mDictPositionRelocationMap->mPtNodeArrayPositionRelocationMap.insert(
            PtNodeWriter::PtNodeArrayPositionRelocationMap::value_type(ptNodeArrayPos, writingPos));
    // Writes dummy PtNode array size because arrays can have a forward link or needles PtNodes.
    // This field will be updated later in onReadingPtNodeArrayTail() with actual PtNode count.
    mPtNodeArraySizeFieldPos = writingPos;
    return DynamicPatriciaTrieWritingUtils::writePtNodeArraySizeAndAdvancePosition(
            mBufferToWrite, 0 /* arraySize */, &writingPos);
}

// Write PtNode array terminal and actual PtNode array size.
bool DynamicPatriciaTrieGcEventListeners::TraversePolicyToPlaceAndWriteValidPtNodesToBuffer
        ::onReadingPtNodeArrayTail() {
    int writingPos = mBufferToWrite->getTailPosition();
    // Write PtNode array terminal.
    if (!DynamicPatriciaTrieWritingUtils::writeForwardLinkPositionAndAdvancePosition(
            mBufferToWrite, NOT_A_DICT_POS /* forwardLinkPos */, &writingPos)) {
        return false;
    }
    // Write actual PtNode array size.
    if (!DynamicPatriciaTrieWritingUtils::writePtNodeArraySizeAndAdvancePosition(
            mBufferToWrite, mValidPtNodeCount, &mPtNodeArraySizeFieldPos)) {
        return false;
    }
    return true;
}

// Write valid PtNode to buffer and memorize mapping from the old position to the new position.
bool DynamicPatriciaTrieGcEventListeners::TraversePolicyToPlaceAndWriteValidPtNodesToBuffer
        ::onVisitingPtNode(const PtNodeParams *const ptNodeParams) {
    if (ptNodeParams->isDeleted()) {
        // Current PtNode is not written in new buffer because it has been deleted.
        mDictPositionRelocationMap->mPtNodePositionRelocationMap.insert(
                PtNodeWriter::PtNodePositionRelocationMap::value_type(
                        ptNodeParams->getHeadPos(), NOT_A_DICT_POS));
        return true;
    }
    int writingPos = mBufferToWrite->getTailPosition();
    mDictPositionRelocationMap->mPtNodePositionRelocationMap.insert(
            PtNodeWriter::PtNodePositionRelocationMap::value_type(
                    ptNodeParams->getHeadPos(), writingPos));
    mValidPtNodeCount++;
    // Writes current PtNode.
    return mPtNodeWriter->writePtNodeAndAdvancePosition(ptNodeParams, &writingPos);
}

bool DynamicPatriciaTrieGcEventListeners::TraversePolicyToUpdateAllPositionFields
        ::onVisitingPtNode(const PtNodeParams *const ptNodeParams) {
    // Updates parent position.
    int bigramCount = 0;
    if (!mPtNodeWriter->updateAllPositionFields(ptNodeParams, mDictPositionRelocationMap,
            &bigramCount)) {
        return false;
    }
    mBigramCount += bigramCount;
    if (ptNodeParams->isTerminal()) {
        mUnigramCount++;
    }
    return true;
}

} // namespace latinime