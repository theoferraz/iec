/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2022, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     EncCu.cpp
    \brief    Coding Unit (CU) encoder class
*/

#include "EncCu.h"

#include "EncLib.h"
#include "Analyze.h"
#include "AQp.h"

#include "CommonLib/dtrace_codingstruct.h"
#include "CommonLib/Picture.h"
#include "CommonLib/UnitTools.h"
#include "MCTS.h"


#include "CommonLib/dtrace_buffer.h"

#include <stdio.h>
#include <cmath>
#include <algorithm>

using namespace std;

//! \ingroup EncoderLib
//! \{

// ====================================================================================================================

const MergeIdxPair EncCu::m_geoModeTest[GEO_MAX_NUM_CANDS] = {
  MergeIdxPair{ 0, 1 }, MergeIdxPair{ 1, 0 }, MergeIdxPair{ 0, 2 }, MergeIdxPair{ 1, 2 }, MergeIdxPair{ 2, 0 },
  MergeIdxPair{ 2, 1 }, MergeIdxPair{ 0, 3 }, MergeIdxPair{ 1, 3 }, MergeIdxPair{ 2, 3 }, MergeIdxPair{ 3, 0 },
  MergeIdxPair{ 3, 1 }, MergeIdxPair{ 3, 2 }, MergeIdxPair{ 0, 4 }, MergeIdxPair{ 1, 4 }, MergeIdxPair{ 2, 4 },
  MergeIdxPair{ 3, 4 }, MergeIdxPair{ 4, 0 }, MergeIdxPair{ 4, 1 }, MergeIdxPair{ 4, 2 }, MergeIdxPair{ 4, 3 },
  MergeIdxPair{ 0, 5 }, MergeIdxPair{ 1, 5 }, MergeIdxPair{ 2, 5 }, MergeIdxPair{ 3, 5 }, MergeIdxPair{ 4, 5 },
  MergeIdxPair{ 5, 0 }, MergeIdxPair{ 5, 1 }, MergeIdxPair{ 5, 2 }, MergeIdxPair{ 5, 3 }, MergeIdxPair{ 5, 4 }
};

EncCu::EncCu() {}

void EncCu::create( EncCfg* encCfg )
{
  unsigned      uiMaxWidth    = encCfg->getMaxCUWidth();
  unsigned      uiMaxHeight   = encCfg->getMaxCUHeight();
  ChromaFormat  chromaFormat  = encCfg->getChromaFormatIdc();

  unsigned      numWidths     = gp_sizeIdxInfo->numWidths();
  unsigned      numHeights    = gp_sizeIdxInfo->numHeights();
  m_pTempCS = new CodingStructure**  [numWidths];
  m_pBestCS = new CodingStructure**  [numWidths];
  m_pTempCS2 = new CodingStructure** [numWidths];
  m_pBestCS2 = new CodingStructure** [numWidths];

  m_pelUnitBufPool.initPelUnitBufPool(chromaFormat, uiMaxWidth, uiMaxHeight);

  for( unsigned w = 0; w < numWidths; w++ )
  {
    m_pTempCS[w] = new CodingStructure*  [numHeights];
    m_pBestCS[w] = new CodingStructure*  [numHeights];
    m_pTempCS2[w] = new CodingStructure* [numHeights];
    m_pBestCS2[w] = new CodingStructure* [numHeights];

    for( unsigned h = 0; h < numHeights; h++ )
    {
      unsigned width  = gp_sizeIdxInfo->sizeFrom( w );
      unsigned height = gp_sizeIdxInfo->sizeFrom( h );

      if( gp_sizeIdxInfo->isCuSize( width ) && gp_sizeIdxInfo->isCuSize( height ) )
      {
        m_pTempCS[w][h] = new CodingStructure(m_unitPool);
        m_pBestCS[w][h] = new CodingStructure(m_unitPool);

#if GDR_ENABLED
        m_pTempCS[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode(), encCfg->getGdrEnabled());
        m_pBestCS[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode(), encCfg->getGdrEnabled());
#else
        m_pTempCS[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode());
        m_pBestCS[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode());
#endif

        m_pTempCS2[w][h] = new CodingStructure(m_unitPool);
        m_pBestCS2[w][h] = new CodingStructure(m_unitPool);

#if GDR_ENABLED
        m_pTempCS2[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode(), encCfg->getGdrEnabled());
        m_pBestCS2[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode(), encCfg->getGdrEnabled());
#else
        m_pTempCS2[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode());
        m_pBestCS2[w][h]->create(chromaFormat, Area(0, 0, width, height), false, (bool)encCfg->getPLTMode());
#endif
      }
      else
      {
        m_pTempCS[w][h] = nullptr;
        m_pBestCS[w][h] = nullptr;
        m_pTempCS2[w][h] = nullptr;
        m_pBestCS2[w][h] = nullptr;
      }
    }
  }

  m_cuChromaQpOffsetIdxPlus1 = 0;

  unsigned maxDepth = numWidths + numHeights;

  m_modeCtrl = new EncModeCtrlMTnoRQT();

  m_modeCtrl->create( *encCfg );

  for (auto &buf: m_geoWeightedBuffers)
  {
    buf.create(chromaFormat, Area(0, 0, uiMaxWidth, uiMaxHeight));
  }

  m_ctxBuffer.resize(maxDepth);
  m_CurrCtx = 0;
}


void EncCu::destroy()
{
  unsigned numWidths  = gp_sizeIdxInfo->numWidths();
  unsigned numHeights = gp_sizeIdxInfo->numHeights();

  for( unsigned w = 0; w < numWidths; w++ )
  {
    for( unsigned h = 0; h < numHeights; h++ )
    {
      if (m_pBestCS[w][h])
      {
        m_pBestCS[w][h]->destroy();
      }
      if (m_pTempCS[w][h])
      {
        m_pTempCS[w][h]->destroy();
      }

      delete m_pBestCS[w][h];
      delete m_pTempCS[w][h];

      if (m_pBestCS2[w][h])
      {
        m_pBestCS2[w][h]->destroy();
      }
      if (m_pTempCS2[w][h])
      {
        m_pTempCS2[w][h]->destroy();
      }

      delete m_pBestCS2[w][h];
      delete m_pTempCS2[w][h];
    }

    delete[] m_pTempCS[w];
    delete[] m_pBestCS[w];
    delete[] m_pTempCS2[w];
    delete[] m_pBestCS2[w];
  }

  delete[] m_pBestCS; m_pBestCS = nullptr;
  delete[] m_pTempCS; m_pTempCS = nullptr;
  delete[] m_pBestCS2; m_pBestCS2 = nullptr;
  delete[] m_pTempCS2; m_pTempCS2 = nullptr;

#if REUSE_CU_RESULTS
  if (m_tmpStorageLCU)
  {
    m_tmpStorageLCU->destroy();
    delete m_tmpStorageLCU;  m_tmpStorageLCU = nullptr;
  }
#endif

#if REUSE_CU_RESULTS
  m_modeCtrl->destroy();

#endif
  delete m_modeCtrl;
  m_modeCtrl = nullptr;

  for (auto &buf: m_geoWeightedBuffers)
  {
    buf.destroy();
  }
}

EncCu::~EncCu()
{
}

/** \param    pcEncLib      pointer of encoder class
 */
void EncCu::init( EncLib* pcEncLib, const SPS& sps )
{
  m_pcEncCfg           = pcEncLib;
  m_pcIntraSearch      = pcEncLib->getIntraSearch();
  m_pcInterSearch      = pcEncLib->getInterSearch();
  m_pcTrQuant          = pcEncLib->getTrQuant();
  m_pcRdCost           = pcEncLib->getRdCost ();
  m_CABACEstimator     = pcEncLib->getCABACEncoder()->getCABACEstimator( &sps );
  m_CABACEstimator->setEncCu(this);
  m_ctxPool            = pcEncLib->getCtxCache();
  m_pcRateCtrl         = pcEncLib->getRateCtrl();
  m_pcSliceEncoder     = pcEncLib->getSliceEncoder();
  m_deblockingFilter   = pcEncLib->getDeblockingFilter();
  m_geoCostList.init(m_pcEncCfg->getMaxNumGeoCand());
  m_AFFBestSATDCost = MAX_DOUBLE;

  DecCu::init( m_pcTrQuant, m_pcIntraSearch, m_pcInterSearch );

  m_modeCtrl->init( m_pcEncCfg, m_pcRateCtrl, m_pcRdCost );
  m_modeCtrl->setBIMQPMap( m_pcEncCfg->getAdaptQPmap() );

  m_pcInterSearch->setModeCtrl( m_modeCtrl );
  m_modeCtrl->setInterSearch(m_pcInterSearch);
  m_pcIntraSearch->setModeCtrl( m_modeCtrl );
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

void EncCu::compressCtu(CodingStructure &cs, const UnitArea &area, const unsigned ctuRsAddr,
                        const EnumArray<int, ChannelType> &prevQP, const EnumArray<int, ChannelType> &currQP)
{
  m_modeCtrl->initCTUEncoding( *cs.slice );
  cs.treeType = TREE_D;

  cs.slice->m_mapPltCost[0].clear();
  cs.slice->m_mapPltCost[1].clear();
  // init the partitioning manager
  QTBTPartitioner partitioner;
  partitioner.initCtu(area, ChannelType::LUMA, *cs.slice);
  if (m_pcEncCfg->getIBCMode())
  {
    if (area.lx() == 0 && area.ly() == 0)
    {
      m_pcInterSearch->resetIbcSearch();
    }
    m_pcInterSearch->resetCtuRecord();
    m_ctuIbcSearchRangeX = m_pcEncCfg->getIBCLocalSearchRangeX();
    m_ctuIbcSearchRangeY = m_pcEncCfg->getIBCLocalSearchRangeY();
  }
  if (m_pcEncCfg->getIBCMode() && m_pcEncCfg->getIBCHashSearch() && (m_pcEncCfg->getIBCFastMethod() & IBC_FAST_METHOD_ADAPTIVE_SEARCHRANGE))
  {
    const int hashHitRatio = m_ibcHashMap.getHashHitRatio(area.Y()); // in percent
    if (hashHitRatio < 5) // 5%
    {
      m_ctuIbcSearchRangeX >>= 1;
      m_ctuIbcSearchRangeY >>= 1;
    }
    if (cs.slice->getNumRefIdx(REF_PIC_LIST_0) > 0)
    {
      m_ctuIbcSearchRangeX >>= 1;
      m_ctuIbcSearchRangeY >>= 1;
    }
  }
  // init current context pointer
  m_CurrCtx = m_ctxBuffer.data();

  CodingStructure *tempCS = m_pTempCS[gp_sizeIdxInfo->idxFrom( area.lumaSize().width )][gp_sizeIdxInfo->idxFrom( area.lumaSize().height )];
  CodingStructure *bestCS = m_pBestCS[gp_sizeIdxInfo->idxFrom( area.lumaSize().width )][gp_sizeIdxInfo->idxFrom( area.lumaSize().height )];

  cs.initSubStructure(*tempCS, partitioner.chType, partitioner.currArea(), false);
  cs.initSubStructure(*bestCS, partitioner.chType, partitioner.currArea(), false);
  tempCS->currQP[ChannelType::LUMA] = bestCS->currQP[ChannelType::LUMA] = tempCS->baseQP = bestCS->baseQP =
    currQP[ChannelType::LUMA];
  tempCS->prevQP[ChannelType::LUMA] = bestCS->prevQP[ChannelType::LUMA] = prevQP[ChannelType::LUMA];

  xCompressCU(tempCS, bestCS, partitioner);
  cs.slice->m_mapPltCost[0].clear();
  cs.slice->m_mapPltCost[1].clear();
  // all signals were already copied during compression if the CTU was split - at this point only the structures are copied to the top level CS
  const bool copyUnsplitCTUSignals = bestCS->cus.size() == 1;
  cs.useSubStructure(*bestCS, partitioner.chType, CS::getArea(*bestCS, area, partitioner.chType), copyUnsplitCTUSignals,
                     false, false, copyUnsplitCTUSignals, true);

  if (CS::isDualITree (cs) && isChromaEnabled (cs.pcv->chrFormat))
  {
    m_CABACEstimator->getCtx() = m_CurrCtx->start;

    partitioner.initCtu(area, ChannelType::CHROMA, *cs.slice);

    cs.initSubStructure(*tempCS, partitioner.chType, partitioner.currArea(), false);
    cs.initSubStructure(*bestCS, partitioner.chType, partitioner.currArea(), false);
    tempCS->currQP[ChannelType::CHROMA] = bestCS->currQP[ChannelType::CHROMA] = tempCS->baseQP = bestCS->baseQP =
      currQP[ChannelType::CHROMA];
    tempCS->prevQP[ChannelType::CHROMA] = bestCS->prevQP[ChannelType::CHROMA] = prevQP[ChannelType::CHROMA];

    xCompressCU(tempCS, bestCS, partitioner);

    const bool copyUnsplitCTUSignals = bestCS->cus.size() == 1;
    cs.useSubStructure(*bestCS, partitioner.chType, CS::getArea(*bestCS, area, partitioner.chType),
                       copyUnsplitCTUSignals, false, false, copyUnsplitCTUSignals, true);
  }

  if (m_pcEncCfg->getUseRateCtrl())
  {
    (m_pcRateCtrl->getRCPic()->getLCU(ctuRsAddr)).m_actualMSE = (double)bestCS->dist / (double)m_pcRateCtrl->getRCPic()->getLCU(ctuRsAddr).m_numberOfPixel;
  }
  // reset context states and uninit context pointer
  m_CABACEstimator->getCtx() = m_CurrCtx->start;
  m_CurrCtx                  = 0;


  // Ensure that a coding was found
  // Selected mode's RD-cost must be not MAX_DOUBLE.
  CHECK( bestCS->cus.empty()                                   , "No possible encoding found" );
  CHECK( bestCS->cus[0]->predMode == NUMBER_OF_PREDICTION_MODES, "No possible encoding found" );
  CHECK( bestCS->cost             == MAX_DOUBLE                , "No possible encoding found" );
}

// ====================================================================================================================
// Protected member functions
// ====================================================================================================================

static int xCalcHADs8x8_ISlice(const Pel *piOrg, const ptrdiff_t strideOrg)
{
  int k, i, j, jj;
  int diff[64], m1[8][8], m2[8][8], m3[8][8], iSumHad = 0;

  for (k = 0; k < 64; k += 8)
  {
    diff[k + 0] = piOrg[0];
    diff[k + 1] = piOrg[1];
    diff[k + 2] = piOrg[2];
    diff[k + 3] = piOrg[3];
    diff[k + 4] = piOrg[4];
    diff[k + 5] = piOrg[5];
    diff[k + 6] = piOrg[6];
    diff[k + 7] = piOrg[7];

    piOrg += strideOrg;
  }

  //horizontal
  for (j = 0; j < 8; j++)
  {
    jj = j << 3;
    m2[j][0] = diff[jj    ] + diff[jj + 4];
    m2[j][1] = diff[jj + 1] + diff[jj + 5];
    m2[j][2] = diff[jj + 2] + diff[jj + 6];
    m2[j][3] = diff[jj + 3] + diff[jj + 7];
    m2[j][4] = diff[jj    ] - diff[jj + 4];
    m2[j][5] = diff[jj + 1] - diff[jj + 5];
    m2[j][6] = diff[jj + 2] - diff[jj + 6];
    m2[j][7] = diff[jj + 3] - diff[jj + 7];

    m1[j][0] = m2[j][0] + m2[j][2];
    m1[j][1] = m2[j][1] + m2[j][3];
    m1[j][2] = m2[j][0] - m2[j][2];
    m1[j][3] = m2[j][1] - m2[j][3];
    m1[j][4] = m2[j][4] + m2[j][6];
    m1[j][5] = m2[j][5] + m2[j][7];
    m1[j][6] = m2[j][4] - m2[j][6];
    m1[j][7] = m2[j][5] - m2[j][7];

    m2[j][0] = m1[j][0] + m1[j][1];
    m2[j][1] = m1[j][0] - m1[j][1];
    m2[j][2] = m1[j][2] + m1[j][3];
    m2[j][3] = m1[j][2] - m1[j][3];
    m2[j][4] = m1[j][4] + m1[j][5];
    m2[j][5] = m1[j][4] - m1[j][5];
    m2[j][6] = m1[j][6] + m1[j][7];
    m2[j][7] = m1[j][6] - m1[j][7];
  }

  //vertical
  for (i = 0; i < 8; i++)
  {
    m3[0][i] = m2[0][i] + m2[4][i];
    m3[1][i] = m2[1][i] + m2[5][i];
    m3[2][i] = m2[2][i] + m2[6][i];
    m3[3][i] = m2[3][i] + m2[7][i];
    m3[4][i] = m2[0][i] - m2[4][i];
    m3[5][i] = m2[1][i] - m2[5][i];
    m3[6][i] = m2[2][i] - m2[6][i];
    m3[7][i] = m2[3][i] - m2[7][i];

    m1[0][i] = m3[0][i] + m3[2][i];
    m1[1][i] = m3[1][i] + m3[3][i];
    m1[2][i] = m3[0][i] - m3[2][i];
    m1[3][i] = m3[1][i] - m3[3][i];
    m1[4][i] = m3[4][i] + m3[6][i];
    m1[5][i] = m3[5][i] + m3[7][i];
    m1[6][i] = m3[4][i] - m3[6][i];
    m1[7][i] = m3[5][i] - m3[7][i];

    m2[0][i] = m1[0][i] + m1[1][i];
    m2[1][i] = m1[0][i] - m1[1][i];
    m2[2][i] = m1[2][i] + m1[3][i];
    m2[3][i] = m1[2][i] - m1[3][i];
    m2[4][i] = m1[4][i] + m1[5][i];
    m2[5][i] = m1[4][i] - m1[5][i];
    m2[6][i] = m1[6][i] + m1[7][i];
    m2[7][i] = m1[6][i] - m1[7][i];
  }

  for (i = 0; i < 8; i++)
  {
    for (j = 0; j < 8; j++)
    {
      iSumHad += abs(m2[i][j]);
    }
  }
  iSumHad -= abs(m2[0][0]);
  iSumHad = (iSumHad + 2) >> 2;
  return(iSumHad);
}

int  EncCu::updateCtuDataISlice(const CPelBuf buf)
{
  int  xBl, yBl;
  const int iBlkSize = 8;
  const Pel* pOrgInit = buf.buf;
  ptrdiff_t  iStrideOrig = buf.stride;

  int iSumHad = 0;
  for( yBl = 0; ( yBl + iBlkSize ) <= buf.height; yBl += iBlkSize )
  {
    for( xBl = 0; ( xBl + iBlkSize ) <= buf.width; xBl += iBlkSize )
    {
      const Pel* pOrg = pOrgInit + iStrideOrig*yBl + xBl;
      iSumHad += xCalcHADs8x8_ISlice( pOrg, iStrideOrig );
    }
  }
  return( iSumHad );
}

bool EncCu::xCheckBestMode( CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode )
{
  bool bestCSUpdated = false;

  if( !tempCS->cus.empty() )
  {
    if( tempCS->cus.size() == 1 )
    {
      const CodingUnit& cu = *tempCS->cus.front();
      CHECK( cu.skip && !cu.firstPU->mergeFlag, "Skip flag without a merge flag is not allowed!" );
    }

#if WCG_EXT
    DTRACE_BEST_MODE( tempCS, bestCS, m_pcRdCost->getLambda( true ) );
#else
    DTRACE_BEST_MODE( tempCS, bestCS, m_pcRdCost->getLambda() );
#endif

    if( m_modeCtrl->useModeResult( encTestMode, tempCS, partitioner ) )
    {
      std::swap( tempCS, bestCS );
      // store temp best CI for next CU coding
      m_CurrCtx->best = m_CABACEstimator->getCtx();
      m_bestModeUpdated = true;
      bestCSUpdated = true;
    }
  }

  // reset context states
  m_CABACEstimator->getCtx() = m_CurrCtx->start;
  return bestCSUpdated;
}

void EncCu::xCompressCU( CodingStructure*& tempCS, CodingStructure*& bestCS, Partitioner& partitioner, double maxCostAllowed )
{
  CHECK(maxCostAllowed < 0, "Wrong value of maxCostAllowed!");

  uint32_t compBegin;
  uint32_t numComp;
  bool jointPLT = false;
  if (partitioner.isSepTree( *tempCS ))
  {
    if( !CS::isDualITree(*tempCS) && partitioner.treeType != TREE_D )
    {
      compBegin = COMPONENT_Y;
      numComp = (tempCS->area.chromaFormat != CHROMA_400)?3: 1;
      jointPLT = true;
    }
    else
    {
      if (isLuma(partitioner.chType))
      {
        compBegin = COMPONENT_Y;
        numComp   = 1;
      }
      else
      {
        compBegin = COMPONENT_Cb;
        numComp   = 2;
      }
    }
  }
  else
  {
    compBegin = COMPONENT_Y;
    numComp = (tempCS->area.chromaFormat != CHROMA_400) ? 3 : 1;
    jointPLT = true;
  }
  SplitSeries splitmode = -1;
  uint8_t   bestLastPLTSize[MAX_NUM_CHANNEL_TYPE];
  Pel       bestLastPLT[MAX_NUM_COMPONENT][MAXPLTPREDSIZE]; // store LastPLT for
  uint8_t   curLastPLTSize[MAX_NUM_CHANNEL_TYPE];
  Pel       curLastPLT[MAX_NUM_COMPONENT][MAXPLTPREDSIZE]; // store LastPLT if no partition
  for (int i = compBegin; i < (compBegin + numComp); i++)
  {
    ComponentID comID = jointPLT ? (ComponentID)compBegin : ((i > 0) ? COMPONENT_Cb : COMPONENT_Y);
    bestLastPLTSize[comID] = 0;
    curLastPLTSize[comID] = tempCS->prevPLT.curPLTSize[comID];
    memcpy(curLastPLT[i], tempCS->prevPLT.curPLT[i], tempCS->prevPLT.curPLTSize[comID] * sizeof(Pel));
  }

  Slice&   slice      = *tempCS->slice;
  const PPS &pps      = *tempCS->pps;
  const SPS &sps      = *tempCS->sps;
  const uint32_t uiLPelX  = tempCS->area.Y().lumaPos().x;
  const uint32_t uiTPelY  = tempCS->area.Y().lumaPos().y;

  const ModeType modeTypeParent  = partitioner.modeType;
  const TreeType treeTypeParent  = partitioner.treeType;
  const ChannelType chTypeParent = partitioner.chType;
  const UnitArea currCsArea = clipArea( CS::getArea( *bestCS, bestCS->area, partitioner.chType ), *tempCS->picture );

  tempCS->splitRdCostBest = nullptr;
  m_modeCtrl->initCULevel( partitioner, *tempCS );
#if GDR_ENABLED
  if (m_pcEncCfg->getGdrEnabled())
  {
    bool isInGdrInterval = slice.getPicHeader()->getInGdrInterval();

    // 1.0 applicable to inter picture only
    if (isInGdrInterval)
    {
      int gdrPocStart = m_pcEncCfg->getGdrPocStart();
      int gdrInterval = m_pcEncCfg->getGdrInterval();
      int gdrPeriod = m_pcEncCfg->getGdrPeriod();

      int picWidth = slice.getPPS()->getPicWidthInLumaSamples();
      int m1, m2, n1;

      int curPoc = slice.getPOC();
      int gdrPoc = (curPoc - gdrPocStart) % gdrPeriod;

      int begGdrX = 0;
      int endGdrX = 0;

      double dd = (picWidth / (double)gdrInterval);
      int mm = (int)((picWidth / (double)gdrInterval) + 0.49999);
      m1 = ((mm + 7) >> 3) << 3;
      m2 = ((mm + 0) >> 3) << 3;

      if (dd > mm && m1 == m2)
      {
        m1 = m1 + 8;
      }

      n1 = (picWidth - m2 * gdrInterval) / 8;

      if (gdrPoc < n1)
      {
        begGdrX = m1 * gdrPoc;
        endGdrX = begGdrX + m1;
      }
      else
      {
        begGdrX = m1 * n1 + m2 * (gdrPoc - n1);
        endGdrX = begGdrX + m2;
        if (picWidth <= endGdrX)
        {
          begGdrX = picWidth;
          endGdrX = picWidth;
        }
      }

      bool isInRefreshArea = tempCS->withinRefresh(begGdrX, endGdrX);

      if (isInRefreshArea)
      {
        m_modeCtrl->forceIntraMode();
      }
      else if (tempCS->containRefresh(begGdrX, endGdrX) || tempCS->overlapRefresh(begGdrX, endGdrX))
      {
        // 1.3.1 enable only vertical splits (QT, BT_V, TT_V)
        m_modeCtrl->forceVerSplitOnly();

        // 1.3.2 remove TT_V if it does not satisfy the condition
        if (tempCS->refreshCrossTTV(begGdrX, endGdrX))
        {
          m_modeCtrl->forceRemoveTTV();
        }
      }

      if (tempCS->area.lwidth() != tempCS->area.lheight())
      {
        m_modeCtrl->forceRemoveQT();
      }

      if (!m_modeCtrl->anyPredModeLeft())
      {
        m_modeCtrl->forceRemoveDontSplit();
      }

      if (isInRefreshArea && !m_modeCtrl->anyIntraIBCMode() && (tempCS->area.lwidth() == 4 || tempCS->area.lheight() == 4))
      {
        m_modeCtrl->finishCULevel(partitioner);
        return;
      }
    }
  }
#endif

  if (partitioner.currQtDepth == 0 && partitioner.currMtDepth == 0 && !tempCS->slice->isIntra()
      && (sps.getUseSBT() || sps.getExplicitMtsInterEnabled()))
  {
    auto slsSbt = dynamic_cast<SaveLoadEncInfoSbt*>( m_modeCtrl );
    int maxSLSize = sps.getUseSBT() ? tempCS->slice->getSPS()->getMaxTbSize() : MTS_INTER_MAX_CU_SIZE;
    slsSbt->resetSaveloadSbt( maxSLSize );
  }
  m_sbtCostSave[0] = m_sbtCostSave[1] = MAX_DOUBLE;

  m_CurrCtx->start = m_CABACEstimator->getCtx();

  if( slice.getUseChromaQpAdj() )
  {
    // TODO M0133 : double check encoder decisions with respect to chroma QG detection and actual encode
    int lgMinCuSize = sps.getLog2MinCodingBlockSize() +
      std::max<int>(0, floorLog2(sps.getCTUSize()) - sps.getLog2MinCodingBlockSize() - int((slice.getCuChromaQpOffsetSubdiv()+1) / 2));
    if( partitioner.currQgChromaEnable() )
    {
      m_cuChromaQpOffsetIdxPlus1 = ( ( uiLPelX >> lgMinCuSize ) + ( uiTPelY >> lgMinCuSize ) ) % ( pps.getChromaQpOffsetListLen() + 1 );
    }
  }
  else
  {
    m_cuChromaQpOffsetIdxPlus1 = 0;
  }

  if( !m_modeCtrl->anyMode() )
  {
    m_modeCtrl->finishCULevel( partitioner );
    return;
  }

  DTRACE_UPDATE( g_trace_ctx, std::make_pair( "cux", uiLPelX ) );
  DTRACE_UPDATE( g_trace_ctx, std::make_pair( "cuy", uiTPelY ) );
  DTRACE_UPDATE( g_trace_ctx, std::make_pair( "cuw", tempCS->area.lwidth() ) );
  DTRACE_UPDATE( g_trace_ctx, std::make_pair( "cuh", tempCS->area.lheight() ) );
  DTRACE( g_trace_ctx, D_COMMON, "@(%4d,%4d) [%2dx%2d]\n", tempCS->area.lx(), tempCS->area.ly(), tempCS->area.lwidth(), tempCS->area.lheight() );


  m_pcInterSearch->resetSavedAffineMotion();

  double bestIntPelCost = MAX_DOUBLE;

  if (tempCS->slice->getSPS()->getUseColorTrans())
  {
    tempCS->tmpColorSpaceCost = MAX_DOUBLE;
    bestCS->tmpColorSpaceCost = MAX_DOUBLE;
    tempCS->firstColorSpaceSelected = true;
    bestCS->firstColorSpaceSelected = true;
  }

  if (tempCS->slice->getSPS()->getUseColorTrans() && !CS::isDualITree(*tempCS))
  {
    tempCS->firstColorSpaceTestOnly = false;
    bestCS->firstColorSpaceTestOnly = false;
    tempCS->tmpColorSpaceIntraCost[0] = MAX_DOUBLE;
    tempCS->tmpColorSpaceIntraCost[1] = MAX_DOUBLE;
    bestCS->tmpColorSpaceIntraCost[0] = MAX_DOUBLE;
    bestCS->tmpColorSpaceIntraCost[1] = MAX_DOUBLE;

    if (tempCS->bestParent && tempCS->bestParent->firstColorSpaceTestOnly)
    {
      tempCS->firstColorSpaceTestOnly = bestCS->firstColorSpaceTestOnly = true;
    }
  }

  double splitRdCostBest[NUM_PART_SPLIT];
  std::fill(std::begin(splitRdCostBest), std::end(splitRdCostBest), MAX_DOUBLE);
  if (tempCS->slice->getCheckLDC())
  {
    m_bestBcwCost.fill(std::numeric_limits<double>::max());
    m_bestBcwIdx.fill(BCW_NUM);
  }
  do
  {
    for (int i = compBegin; i < (compBegin + numComp); i++)
    {
      ComponentID comID = jointPLT ? (ComponentID)compBegin : ((i > 0) ? COMPONENT_Cb : COMPONENT_Y);
      tempCS->prevPLT.curPLTSize[comID] = curLastPLTSize[comID];
      memcpy(tempCS->prevPLT.curPLT[i], curLastPLT[i], curLastPLTSize[comID] * sizeof(Pel));
    }
    EncTestMode currTestMode = m_modeCtrl->currTestMode();
    currTestMode.maxCostAllowed = maxCostAllowed;

    if (pps.getUseDQP() && partitioner.isSepTree(*tempCS) && isChroma( partitioner.chType ))
    {
      const Position chromaCentral(tempCS->area.Cb().chromaPos().offset(tempCS->area.Cb().chromaSize().width >> 1, tempCS->area.Cb().chromaSize().height >> 1));
      const Position lumaRefPos(chromaCentral.x << getComponentScaleX(COMPONENT_Cb, tempCS->area.chromaFormat), chromaCentral.y << getComponentScaleY(COMPONENT_Cb, tempCS->area.chromaFormat));
      const CodingStructure* baseCS = bestCS->picture->cs;
      const CodingUnit      *colLumaCu = baseCS->getCU(lumaRefPos, ChannelType::LUMA);

      if (colLumaCu)
      {
        currTestMode.qp = colLumaCu->qp;
      }
    }

#if SHARP_LUMA_DELTA_QP || ENABLE_QPA_SUB_CTU
    if (partitioner.currQgEnable() && (
        (m_pcEncCfg->getBIM()) ||
#if SHARP_LUMA_DELTA_QP
        (m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled()) ||
#endif
        (m_pcEncCfg->getSmoothQPReductionEnable()) ||
#if ENABLE_QPA_SUB_CTU
        (m_pcEncCfg->getUsePerceptQPA() && !m_pcEncCfg->getUseRateCtrl() && pps.getUseDQP())
#else
        false
#endif
      ))
    {
      if (currTestMode.qp >= 0)
      {
        updateLambda (&slice, currTestMode.qp,
 #if WCG_EXT && ER_CHROMA_QP_WCG_PPS
                      m_pcEncCfg->getWCGChromaQPControl().isEnabled(),
 #endif
                      CS::isDualITree (*tempCS) || (partitioner.currDepth == 0));
      }
    }
#endif

    if( currTestMode.type == ETM_INTER_ME )
    {
      if( ( currTestMode.opts & ETO_IMV ) != 0 )
      {
        const bool skipAltHpelIF = (currTestMode.getAmvrSearchMode() == EncTestMode::AmvrSearchMode::HALF_PEL)
                                   && (bestIntPelCost > 1.25 * bestCS->cost);
        if (!skipAltHpelIF)
        {
          tempCS->bestCS = bestCS;
          xCheckRDCostInterAmvr(tempCS, bestCS, partitioner, currTestMode, bestIntPelCost);
          tempCS->bestCS = nullptr;
          splitRdCostBest[CTU_LEVEL] = bestCS->cost;
          tempCS->splitRdCostBest = splitRdCostBest;
        }
      }
      else
      {
        tempCS->bestCS = bestCS;
        xCheckRDCostInter( tempCS, bestCS, partitioner, currTestMode );
        tempCS->bestCS = nullptr;
        splitRdCostBest[CTU_LEVEL] = bestCS->cost;
        tempCS->splitRdCostBest = splitRdCostBest;
      }

    }
    else if (currTestMode.type == ETM_HASH_INTER)
    {
      xCheckRDCostHashInter( tempCS, bestCS, partitioner, currTestMode );
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
    else if( currTestMode.type == ETM_AFFINE )
    {
      xCheckRDCostAffineMerge2Nx2N( tempCS, bestCS, partitioner, currTestMode );
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
#if REUSE_CU_RESULTS
    else if( currTestMode.type == ETM_RECO_CACHED )
    {
      xReuseCachedResult( tempCS, bestCS, partitioner );
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
#endif
    else if( currTestMode.type == ETM_MERGE_SKIP )
    {
      xCheckRDCostMerge2Nx2N( tempCS, bestCS, partitioner, currTestMode );
      CodingUnit* cu = bestCS->getCU(partitioner.chType);
      if (cu)
      {
        cu->mmvdSkip = cu->skip == false ? false : cu->mmvdSkip;
      }
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
    else if( currTestMode.type == ETM_MERGE_GEO )
    {
      xCheckRDCostMergeGeo2Nx2N( tempCS, bestCS, partitioner, currTestMode );
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
    else if( currTestMode.type == ETM_INTRA )
    {
      if (slice.getSPS()->getUseColorTrans() && !CS::isDualITree(*tempCS))
      {
        bool skipSecColorSpace = false;
        skipSecColorSpace = xCheckRDCostIntra(tempCS, bestCS, partitioner, currTestMode, (m_pcEncCfg->getRGBFormatFlag() ? true : false));
        if ((m_pcEncCfg->getCostMode() == COST_LOSSLESS_CODING && slice.isLossless()) && !m_pcEncCfg->getRGBFormatFlag())
        {
          skipSecColorSpace = true;
        }
        if (!skipSecColorSpace && !tempCS->firstColorSpaceTestOnly)
        {
          xCheckRDCostIntra(tempCS, bestCS, partitioner, currTestMode, (m_pcEncCfg->getRGBFormatFlag() ? false : true));
        }

        if (!tempCS->firstColorSpaceTestOnly)
        {
          if (tempCS->tmpColorSpaceIntraCost[0] != MAX_DOUBLE && tempCS->tmpColorSpaceIntraCost[1] != MAX_DOUBLE)
          {
            double skipCostRatio = m_pcEncCfg->getRGBFormatFlag() ? 1.1 : 1.0;
            if (tempCS->tmpColorSpaceIntraCost[1] > (skipCostRatio*tempCS->tmpColorSpaceIntraCost[0]))
            {
              tempCS->firstColorSpaceTestOnly = bestCS->firstColorSpaceTestOnly = true;
            }
          }
        }
        else
        {
          CHECK(tempCS->tmpColorSpaceIntraCost[1] != MAX_DOUBLE, "the RD test of the second color space should be skipped");
        }
      }
      else
      {
        xCheckRDCostIntra(tempCS, bestCS, partitioner, currTestMode, false);
      }
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
    else if (currTestMode.type == ETM_PALETTE)
    {
      xCheckPLT( tempCS, bestCS, partitioner, currTestMode );
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
    else if (currTestMode.type == ETM_IBC)
    {
      xCheckRDCostIBCMode(tempCS, bestCS, partitioner, currTestMode);
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
    else if (currTestMode.type == ETM_IBC_MERGE)
    {
      xCheckRDCostIBCModeMerge2Nx2N(tempCS, bestCS, partitioner, currTestMode);
      splitRdCostBest[CTU_LEVEL] = bestCS->cost;
      tempCS->splitRdCostBest = splitRdCostBest;
    }
    else if( isModeSplit( currTestMode ) )
    {
      if (bestCS->cus.size() != 0)
      {
        splitmode = bestCS->cus[0]->splitSeries;
      }
      assert( partitioner.modeType == tempCS->modeType );
      int signalModeConsVal = tempCS->signalModeCons( getPartSplit( currTestMode ), partitioner, modeTypeParent );
      int numRoundRdo = signalModeConsVal == LDT_MODE_TYPE_SIGNAL ? 2 : 1;
      bool skipInterPass = false;
      for( int i = 0; i < numRoundRdo; i++ )
      {
        //change cons modes
        if( signalModeConsVal == LDT_MODE_TYPE_SIGNAL )
        {
          CHECK( numRoundRdo != 2, "numRoundRdo shall be 2 - [LDT_MODE_TYPE_SIGNAL]" );
          tempCS->modeType = partitioner.modeType = (i == 0) ? MODE_TYPE_INTER : MODE_TYPE_INTRA;
        }
        else if( signalModeConsVal == LDT_MODE_TYPE_INFER )
        {
          CHECK( numRoundRdo != 1, "numRoundRdo shall be 1 - [LDT_MODE_TYPE_INFER]" );
          tempCS->modeType = partitioner.modeType = MODE_TYPE_INTRA;
        }
        else if( signalModeConsVal == LDT_MODE_TYPE_INHERIT )
        {
          CHECK( numRoundRdo != 1, "numRoundRdo shall be 1 - [LDT_MODE_TYPE_INHERIT]" );
          tempCS->modeType = partitioner.modeType = modeTypeParent;
        }

        //for lite intra encoding fast algorithm, set the status to save inter coding info
        if( modeTypeParent == MODE_TYPE_ALL && tempCS->modeType == MODE_TYPE_INTER )
        {
          m_pcIntraSearch->setSaveCuCostInSCIPU( true );
          m_pcIntraSearch->setNumCuInSCIPU( 0 );
        }
        else if( modeTypeParent == MODE_TYPE_ALL && tempCS->modeType != MODE_TYPE_INTER )
        {
          m_pcIntraSearch->setSaveCuCostInSCIPU( false );
          if( tempCS->modeType == MODE_TYPE_ALL )
          {
            m_pcIntraSearch->setNumCuInSCIPU( 0 );
          }
        }

        xCheckModeSplit( tempCS, bestCS, partitioner, currTestMode, modeTypeParent, skipInterPass, splitRdCostBest );
        tempCS->splitRdCostBest = splitRdCostBest;
        //recover cons modes
        tempCS->modeType = partitioner.modeType = modeTypeParent;
        tempCS->treeType = partitioner.treeType = treeTypeParent;
        partitioner.chType = chTypeParent;
        if( modeTypeParent == MODE_TYPE_ALL )
        {
          m_pcIntraSearch->setSaveCuCostInSCIPU( false );
          if( numRoundRdo == 2 && tempCS->modeType == MODE_TYPE_INTRA )
          {
            m_pcIntraSearch->initCuAreaCostInSCIPU();
          }
        }
        if( skipInterPass )
        {
          break;
        }
      }
#if GDR_ENABLED
      if (bestCS->cus.size() > 0 && splitmode != bestCS->cus[0]->splitSeries)
#else
      if (splitmode != bestCS->cus[0]->splitSeries)
#endif
      {
        splitmode = bestCS->cus[0]->splitSeries;
        const CodingUnit&     cu = *bestCS->cus.front();
        cu.cs->prevPLT = bestCS->prevPLT;
        for (int i = compBegin; i < (compBegin + numComp); i++)
        {
          ComponentID comID = jointPLT ? (ComponentID)compBegin : ((i > 0) ? COMPONENT_Cb : COMPONENT_Y);
          bestLastPLTSize[comID] = bestCS->cus[0]->cs->prevPLT.curPLTSize[comID];
          memcpy(bestLastPLT[i], bestCS->cus[0]->cs->prevPLT.curPLT[i], bestCS->cus[0]->cs->prevPLT.curPLTSize[comID] * sizeof(Pel));
        }
      }
    }
    else
    {
      THROW( "Don't know how to handle mode: type = " << currTestMode.type << ", options = " << currTestMode.opts );
    }
  } while( m_modeCtrl->nextMode( *tempCS, partitioner ) );


  //////////////////////////////////////////////////////////////////////////
  // Finishing CU
  if( tempCS->cost == MAX_DOUBLE && bestCS->cost == MAX_DOUBLE )
  {
    //although some coding modes were planned to be tried in RDO, no coding mode actually finished encoding due to early termination
    //thus tempCS->cost and bestCS->cost are both MAX_DOUBLE; in this case, skip the following process for normal case
    m_modeCtrl->finishCULevel( partitioner );
    return;
  }

  // set context states
  m_CABACEstimator->getCtx() = m_CurrCtx->best;

  // QP from last processed CU for further processing
  //copy the qp of the last non-chroma CU
  int numCUInThisNode = (int)bestCS->cus.size();
  if (numCUInThisNode > 1 && bestCS->cus.back()->chType == ChannelType::CHROMA && !CS::isDualITree(*bestCS))
  {
    CHECK(bestCS->cus[numCUInThisNode - 2]->chType != ChannelType::LUMA, "wrong chType");
    bestCS->prevQP[partitioner.chType] = bestCS->cus[numCUInThisNode - 2]->qp;
  }
  else
  {
    bestCS->prevQP[partitioner.chType] = bestCS->cus.back()->qp;
  }
  if ((!slice.isIntra() || slice.getSPS()->getIBCFlag()) && isLuma(partitioner.chType) && bestCS->cus.size() == 1
      && (CU::isInter(*bestCS->cus.back()) || CU::isIBC(*bestCS->cus.back()))
      && bestCS->area.Y() == (*bestCS->cus.back()).Y())
  {
    const CodingUnit&     cu = *bestCS->cus.front();

    CU::saveMotionForHmvp(cu);
  }
  bestCS->picture->getPredBuf(currCsArea).copyFrom(bestCS->getPredBuf(currCsArea));
  bestCS->picture->getRecoBuf( currCsArea ).copyFrom( bestCS->getRecoBuf( currCsArea ) );
  m_modeCtrl->finishCULevel( partitioner );
  if( m_pcIntraSearch->getSaveCuCostInSCIPU() && bestCS->cus.size() == 1 )
  {
    m_pcIntraSearch->saveCuAreaCostInSCIPU( Area( partitioner.currArea().lumaPos(), partitioner.currArea().lumaSize() ), bestCS->cost );
  }

  if (bestCS->cus.size() == 1) // no partition
  {
    CHECK(bestCS->cus[0]->tileIdx != bestCS->pps->getTileIdx(bestCS->area.lumaPos()), "Wrong tile index!");
    if (CU::isPLT(*bestCS->cus[0]))
    {
      for (int i = compBegin; i < (compBegin + numComp); i++)
      {
        ComponentID comID = jointPLT ? (ComponentID)compBegin : ((i > 0) ? COMPONENT_Cb : COMPONENT_Y);
        bestCS->prevPLT.curPLTSize[comID] = curLastPLTSize[comID];
        memcpy(bestCS->prevPLT.curPLT[i], curLastPLT[i], curLastPLTSize[comID] * sizeof(Pel));
      }
      bestCS->reorderPrevPLT(bestCS->prevPLT, bestCS->cus[0]->curPLTSize, bestCS->cus[0]->curPLT, bestCS->cus[0]->reuseflag, compBegin, numComp, jointPLT);
    }
    else
    {
      for (int i = compBegin; i<(compBegin + numComp); i++)
      {
        ComponentID comID = jointPLT ? (ComponentID)compBegin : ((i > 0) ? COMPONENT_Cb : COMPONENT_Y);
        bestCS->prevPLT.curPLTSize[comID] = curLastPLTSize[comID];
        memcpy(bestCS->prevPLT.curPLT[i], curLastPLT[i], bestCS->prevPLT.curPLTSize[comID] * sizeof(Pel));
      }
    }
  }
  else
  {
    for (int i = compBegin; i<(compBegin + numComp); i++)
    {
      ComponentID comID = jointPLT ? (ComponentID)compBegin : ((i > 0) ? COMPONENT_Cb : COMPONENT_Y);
      bestCS->prevPLT.curPLTSize[comID] = bestLastPLTSize[comID];
      memcpy(bestCS->prevPLT.curPLT[i], bestLastPLT[i], bestCS->prevPLT.curPLTSize[comID] * sizeof(Pel));
    }
  }
  const CodingUnit&     cu = *bestCS->cus.front();
  cu.cs->prevPLT = bestCS->prevPLT;
  // Assert if Best prediction mode is NONE
  // Selected mode's RD-cost must be not MAX_DOUBLE.
  CHECK( bestCS->cus.empty()                                   , "No possible encoding found" );
  CHECK( bestCS->cus[0]->predMode == NUMBER_OF_PREDICTION_MODES, "No possible encoding found" );
  CHECK( bestCS->cost             == MAX_DOUBLE                , "No possible encoding found" );
}

#if SHARP_LUMA_DELTA_QP || ENABLE_QPA_SUB_CTU
void EncCu::updateLambda(Slice *slice, const int dQP,
#if WCG_EXT && ER_CHROMA_QP_WCG_PPS
                         const bool useWCGChromaControl,
#endif
                         const bool updateRdCostLambda)
{
#if WCG_EXT && ER_CHROMA_QP_WCG_PPS
  if (useWCGChromaControl)
  {
    const double lambda = m_pcSliceEncoder->initializeLambda (slice, m_pcSliceEncoder->getGopId(), slice->getSliceQp(), (double)dQP);
    const int clippedQP = Clip3(-slice->getSPS()->getQpBDOffset(ChannelType::LUMA), MAX_QP, dQP);

    m_pcSliceEncoder->setUpLambda (slice, lambda, clippedQP);
    return;
  }
#endif
  int          qp        = dQP;
  const double oldQP     = (double)slice->getSliceQpBase();
#if ENABLE_QPA_SUB_CTU
  const double oldLambda =
    (m_pcEncCfg->getUsePerceptQPA() && !m_pcEncCfg->getUseRateCtrl() && slice->getPPS()->getUseDQP())
      ? slice->getLambdas()[0]
      : m_pcSliceEncoder->calculateLambda(slice, m_pcSliceEncoder->getGopId(), oldQP, oldQP, qp);
#else
  const double oldLambda = m_pcSliceEncoder->calculateLambda(slice, m_pcSliceEncoder->getGopId(), oldQP, oldQP, qp);
#endif
  const double newLambda = oldLambda * pow (2.0, ((double)dQP - oldQP) / 3.0);
#if RDOQ_CHROMA_LAMBDA
  const double lambdaArray[MAX_NUM_COMPONENT] = {newLambda / m_pcRdCost->getDistortionWeight (COMPONENT_Y),
                                                 newLambda / m_pcRdCost->getDistortionWeight (COMPONENT_Cb),
                                                 newLambda / m_pcRdCost->getDistortionWeight (COMPONENT_Cr)};
  m_pcTrQuant->setLambdas (lambdaArray);
#else
  m_pcTrQuant->setLambda (newLambda);
#endif
  if (updateRdCostLambda)
  {
    m_pcRdCost->setLambda (newLambda, slice->getSPS()->getBitDepths());
#if WCG_EXT
    if (!m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled())
    {
      m_pcRdCost->saveUnadjustedLambda();
    }
#endif
  }
}
#endif // SHARP_LUMA_DELTA_QP || ENABLE_QPA_SUB_CTU

void EncCu::xCheckModeSplit(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode, const ModeType modeTypeParent, bool &skipInterPass, double *splitRdCostBest )
{
  const int qp                = encTestMode.qp;
  const Slice &slice          = *tempCS->slice;
  const int    oldPrevQp         = tempCS->prevQP[partitioner.chType];
  const auto oldMotionLut     = tempCS->motionLut;
#if ENABLE_QPA_SUB_CTU
  const PPS &pps              = *tempCS->pps;
  const uint32_t currDepth    = partitioner.currDepth;
#endif
  const auto oldPLT           = tempCS->prevPLT;

  const PartSplit split = getPartSplit( encTestMode );
  const ModeType modeTypeChild = partitioner.modeType;

  CHECK( split == CU_DONT_SPLIT, "No proper split provided!" );

  tempCS->initStructData( qp );

  m_CABACEstimator->getCtx() = m_CurrCtx->start;

  const TempCtx ctxStartSP(m_ctxPool, SubCtx(Ctx::SplitFlag, m_CABACEstimator->getCtx()));
  const TempCtx ctxStartQt(m_ctxPool, SubCtx(Ctx::SplitQtFlag, m_CABACEstimator->getCtx()));
  const TempCtx ctxStartHv(m_ctxPool, SubCtx(Ctx::SplitHvFlag, m_CABACEstimator->getCtx()));
  const TempCtx ctxStart12(m_ctxPool, SubCtx(Ctx::Split12Flag, m_CABACEstimator->getCtx()));
  const TempCtx ctxStartMC(m_ctxPool, SubCtx(Ctx::ModeConsFlag, m_CABACEstimator->getCtx()));
  m_CABACEstimator->resetBits();

  m_CABACEstimator->split_cu_mode( split, *tempCS, partitioner );
  m_CABACEstimator->mode_constraint( split, *tempCS, partitioner, modeTypeChild );

  double costTemp = 0;
  if( m_pcEncCfg->getFastAdaptCostPredMode() == 2 )
  {
    int numChild = 3;
    if( split == CU_VERT_SPLIT || split == CU_HORZ_SPLIT )
    {
      numChild--;
    }
    else if( split == CU_QUAD_SPLIT )
    {
      numChild++;
    }

    int64_t approxBits = numChild << SCALE_BITS;

    const double factor =
      (tempCS->currQP[partitioner.chType] > 30 ? 1.11 : 1.085) + (isChroma(partitioner.chType) ? 0.2 : 0.0);

    costTemp = m_pcRdCost->calcRdCost( uint64_t( m_CABACEstimator->getEstFracBits() + approxBits + ( ( bestCS->fracBits ) / factor ) ), Distortion( bestCS->dist / factor ) ) + bestCS->costDbOffset / factor;
  }
  else if (m_pcEncCfg->getFastAdaptCostPredMode() == 1)
  {
    const double factor =
      (tempCS->currQP[partitioner.chType] > 30 ? 1.1 : 1.075) + (isChroma(partitioner.chType) ? 0.2 : 0.0);
    costTemp = m_pcRdCost->calcRdCost( uint64_t( m_CABACEstimator->getEstFracBits() + ( ( bestCS->fracBits ) / factor ) ), Distortion( bestCS->dist / factor ) ) + bestCS->costDbOffset / factor;
  }
  else
  {
    const double factor = (tempCS->currQP[partitioner.chType] > 30 ? 1.1 : 1.075);
    costTemp = m_pcRdCost->calcRdCost( uint64_t( m_CABACEstimator->getEstFracBits() + ( ( bestCS->fracBits ) / factor ) ), Distortion( bestCS->dist / factor ) ) + bestCS->costDbOffset / factor;
  }
  tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();
  if( !tempCS->useDbCost )
  {
    CHECK( bestCS->costDbOffset != 0, "error" );
  }
  const double cost = costTemp;

  m_CABACEstimator->getCtx() = SubCtx( Ctx::SplitFlag,   ctxStartSP );
  m_CABACEstimator->getCtx() = SubCtx( Ctx::SplitQtFlag, ctxStartQt );
  m_CABACEstimator->getCtx() = SubCtx( Ctx::SplitHvFlag, ctxStartHv );
  m_CABACEstimator->getCtx() = SubCtx( Ctx::Split12Flag, ctxStart12 );
  m_CABACEstimator->getCtx() = SubCtx( Ctx::ModeConsFlag, ctxStartMC );
  if (cost > bestCS->cost + bestCS->costDbOffset
#if ENABLE_QPA_SUB_CTU
    || (m_pcEncCfg->getUsePerceptQPA() && !m_pcEncCfg->getUseRateCtrl() && pps.getUseDQP() && (slice.getCuQpDeltaSubdiv() > 0) && (split == CU_HORZ_SPLIT || split == CU_VERT_SPLIT) &&
        (currDepth == 0)) // force quad-split or no split at CTU level
#endif
    )
  {
    xCheckBestMode( tempCS, bestCS, partitioner, encTestMode );
    return;
  }

  const bool chromaNotSplit = modeTypeParent == MODE_TYPE_ALL && modeTypeChild == MODE_TYPE_INTRA ? true : false;
  if( partitioner.treeType != TREE_D )
  {
    tempCS->treeType = TREE_L;
  }
  else
  {
    if( chromaNotSplit )
    {
      CHECK(partitioner.chType != ChannelType::LUMA, "chType must be luma");
      tempCS->treeType = partitioner.treeType = TREE_L;
    }
    else
    {
      tempCS->treeType = partitioner.treeType = TREE_D;
    }
  }


  partitioner.splitCurrArea( split, *tempCS );
  bool qgEnableChildren = partitioner.currQgEnable(); // QG possible at children level
  bool qgChromaEnableChildren = partitioner.currQgChromaEnable(); // Chroma QG possible at children level

  m_CurrCtx++;

  tempCS->getRecoBuf().fill( 0 );

  tempCS->getPredBuf().fill(0);
  AffineMVInfo tmpMVInfo;
  bool isAffMVInfoSaved;
#if GDR_ENABLED
  AffineMVInfoSolid tmpMVInfoSolid;
  m_pcInterSearch->savePrevAffMVInfo(0, tmpMVInfo, tmpMVInfoSolid, isAffMVInfoSaved);
#else
  m_pcInterSearch->savePrevAffMVInfo(0, tmpMVInfo, isAffMVInfoSaved);
#endif
  BlkUniMvInfo tmpUniMvInfo;
  bool         isUniMvInfoSaved = false;
  if (!tempCS->slice->isIntra())
  {
    m_pcInterSearch->savePrevUniMvInfo(tempCS->area.Y(), tmpUniMvInfo, isUniMvInfoSaved);
  }

  do
  {
    const auto &subCUArea  = partitioner.currArea();

    if( tempCS->picture->Y().contains( subCUArea.lumaPos() ) )
    {
      const unsigned wIdx    = gp_sizeIdxInfo->idxFrom( subCUArea.lwidth () );
      const unsigned hIdx    = gp_sizeIdxInfo->idxFrom( subCUArea.lheight() );

      CodingStructure *tempSubCS = m_pTempCS[wIdx][hIdx];
      CodingStructure *bestSubCS = m_pBestCS[wIdx][hIdx];

      tempCS->initSubStructure( *tempSubCS, partitioner.chType, subCUArea, false );
      tempCS->initSubStructure( *bestSubCS, partitioner.chType, subCUArea, false );
      tempSubCS->bestParent = bestSubCS->bestParent = bestCS;
      double newMaxCostAllowed = isLuma(partitioner.chType) ? std::min(encTestMode.maxCostAllowed, bestCS->cost - m_pcRdCost->calcRdCost(tempCS->fracBits, tempCS->dist)) : MAX_DOUBLE;
      newMaxCostAllowed = std::max(0.0, newMaxCostAllowed);
      xCompressCU(tempSubCS, bestSubCS, partitioner, newMaxCostAllowed);
      tempSubCS->bestParent = bestSubCS->bestParent = nullptr;

      if( bestSubCS->cost == MAX_DOUBLE )
      {
        CHECK( split == CU_QUAD_SPLIT, "Split decision reusing cannot skip quad split" );
        tempCS->cost = MAX_DOUBLE;
        tempCS->costDbOffset = 0;
        tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();
        m_CurrCtx--;
        partitioner.exitCurrSplit();
        xCheckBestMode( tempCS, bestCS, partitioner, encTestMode );
        if (isLuma(partitioner.chType))
        {
          tempCS->motionLut = oldMotionLut;
        }
        return;
      }

      bool keepResi = KEEP_PRED_AND_RESI_SIGNALS;
      tempCS->useSubStructure( *bestSubCS, partitioner.chType, CS::getArea( *tempCS, subCUArea, partitioner.chType ), KEEP_PRED_AND_RESI_SIGNALS, true, keepResi, keepResi, true );

      if( partitioner.currQgEnable() )
      {
        tempCS->prevQP[partitioner.chType] = bestSubCS->prevQP[partitioner.chType];
      }
      if( partitioner.isConsInter() )
      {
        for( int i = 0; i < bestSubCS->cus.size(); i++ )
        {
          CHECK(!CU::isInter(*bestSubCS->cus[i]), "all CUs must be inter mode in an Inter coding region (SCIPU)");
        }
      }
      else if( partitioner.isConsIntra() )
      {
        for( int i = 0; i < bestSubCS->cus.size(); i++ )
        {
          CHECK(CU::isInter(*bestSubCS->cus[i]), "all CUs must not be inter mode in an Intra coding region (SCIPU)");
        }
      }

      tempSubCS->releaseIntermediateData();
      bestSubCS->releaseIntermediateData();
      if( !tempCS->slice->isIntra() && partitioner.isConsIntra() )
      {
        tempCS->cost = m_pcRdCost->calcRdCost( tempCS->fracBits, tempCS->dist );
        if( tempCS->cost > bestCS->cost )
        {
          tempCS->cost = MAX_DOUBLE;
          tempCS->costDbOffset = 0;
          tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();
          m_CurrCtx--;
          partitioner.exitCurrSplit();
          if (isLuma(partitioner.chType))
          {
            tempCS->motionLut = oldMotionLut;
          }
          return;
        }
      }
    }
  } while( partitioner.nextPart( *tempCS ) );

  partitioner.exitCurrSplit();


  m_CurrCtx--;

  if( chromaNotSplit )
  {
    //Note: In local dual tree region, the chroma CU refers to the central luma CU's QP.
    //If the luma CU QP shall be predQP (no residual in it and before it in the QG), it must be revised to predQP before encoding the chroma CU
    //Otherwise, the chroma CU uses predQP+deltaQP in encoding but is decoded as using predQP, thus causing encoder-decoded mismatch on chroma qp.
    if( tempCS->pps->getUseDQP() )
    {
      //find parent CS that including all coded CUs in the QG before this node
      CodingStructure* qgCS = tempCS;
      bool deltaQpCodedBeforeThisNode = false;
      if( partitioner.currArea().lumaPos() != partitioner.currQgPos )
      {
        int numParentNodeToQgCS = 0;
        while( qgCS->area.lumaPos() != partitioner.currQgPos )
        {
          CHECK( qgCS->parent == nullptr, "parent of qgCS shall exsit" );
          qgCS = qgCS->parent;
          numParentNodeToQgCS++;
        }

        //check whether deltaQP has been coded (in luma CU or luma&chroma CU) before this node
        CodingStructure* parentCS = tempCS->parent;
        for( int i = 0; i < numParentNodeToQgCS; i++ )
        {
          //checking each parent
          CHECK( parentCS == nullptr, "parentCS shall exsit" );
          for( const auto &cu : parentCS->cus )
          {
            if( cu->rootCbf && !isChroma( cu->chType ) )
            {
              deltaQpCodedBeforeThisNode = true;
              break;
            }
          }
          parentCS = parentCS->parent;
        }
      }

      //revise luma CU qp before the first luma CU with residual in the SCIPU to predQP
      if( !deltaQpCodedBeforeThisNode )
      {
        //get pred QP of the QG
        const CodingUnit *cuFirst = qgCS->getCU(ChannelType::LUMA);
        CHECK( cuFirst->lumaPos() != partitioner.currQgPos, "First cu of the Qg is wrong" );
        const int predQp = CU::predictQP(*cuFirst, qgCS->prevQP[ChannelType::LUMA]);

        //revise to predQP
        int firstCuHasResidual = (int)tempCS->cus.size();
        for( int i = 0; i < tempCS->cus.size(); i++ )
        {
          if( tempCS->cus[i]->rootCbf )
          {
            firstCuHasResidual = i;
            break;
          }
        }

        for( int i = 0; i < firstCuHasResidual; i++ )
        {
          tempCS->cus[i]->qp = predQp;
        }
      }
    }
    assert( tempCS->treeType == TREE_L );
    uint32_t numCuPuTu[6];
    tempCS->picture->cs->getNumCuPuTuOffset( numCuPuTu );
    tempCS->picture->cs->useSubStructure( *tempCS, partitioner.chType, CS::getArea( *tempCS, partitioner.currArea(), partitioner.chType ), false, true, false, false, false );

    if (isChromaEnabled(tempCS->pcv->chrFormat))
    {
      partitioner.chType = ChannelType::CHROMA;
      tempCS->treeType = partitioner.treeType = TREE_C;

      m_CurrCtx++;

      const unsigned   wIdx         = gp_sizeIdxInfo->idxFrom(partitioner.currArea().lwidth());
      const unsigned   hIdx         = gp_sizeIdxInfo->idxFrom(partitioner.currArea().lheight());
      CodingStructure *tempCSChroma = m_pTempCS2[wIdx][hIdx];
      CodingStructure *bestCSChroma = m_pBestCS2[wIdx][hIdx];
      tempCS->initSubStructure(*tempCSChroma, partitioner.chType, partitioner.currArea(), false);
      tempCS->initSubStructure(*bestCSChroma, partitioner.chType, partitioner.currArea(), false);
      tempCS->treeType = TREE_D;
      xCompressCU(tempCSChroma, bestCSChroma, partitioner);

      // attach chromaCS to luma CS and update cost
      bool keepResi = KEEP_PRED_AND_RESI_SIGNALS;
      // bestCSChroma->treeType = tempCSChroma->treeType = TREE_C;
      CHECK(bestCSChroma->treeType != TREE_C || tempCSChroma->treeType != TREE_C, "wrong treeType for chroma CS");
      tempCS->useSubStructure(*bestCSChroma, partitioner.chType,
                              CS::getArea(*bestCSChroma, partitioner.currArea(), partitioner.chType),
                              KEEP_PRED_AND_RESI_SIGNALS, true, keepResi, true, true);

      // release tmp resource
      tempCSChroma->releaseIntermediateData();
      bestCSChroma->releaseIntermediateData();
      // tempCS->picture->cs->releaseIntermediateData();
      m_CurrCtx--;
    }
    tempCS->picture->cs->clearCuPuTuIdxMap( partitioner.currArea(), numCuPuTu[0], numCuPuTu[1], numCuPuTu[2], numCuPuTu + 3 );


    //recover luma tree status
    partitioner.chType   = ChannelType::LUMA;
    partitioner.treeType = TREE_D;
    partitioner.modeType = MODE_TYPE_ALL;
  }
  else
  {
    if (!qgChromaEnableChildren) // check at deepest cQG level only
    {
      xCheckChromaQPOffset( *tempCS, partitioner );
    }
  }

  // Finally, generate split-signaling bits for RD-cost check
  const PartSplit implicitSplit = partitioner.getImplicitSplit( *tempCS );

  {
    bool enforceQT = implicitSplit == CU_QUAD_SPLIT;

    // LARGE CTU bug
    if( m_pcEncCfg->getUseFastLCTU() )
    {
      unsigned minDepth = 0;
      unsigned maxDepth = floorLog2(tempCS->sps->getCTUSize()) - floorLog2(tempCS->sps->getMinQTSize(slice.getSliceType(), partitioner.chType));

      if( auto ad = dynamic_cast<AdaptiveDepthPartitioner*>( &partitioner ) )
      {
        ad->setMaxMinDepth( minDepth, maxDepth, *tempCS );
      }

      if( minDepth > partitioner.currQtDepth )
      {
        // enforce QT
        enforceQT = true;
      }
    }

    if( !enforceQT )
    {
      m_CABACEstimator->resetBits();

      m_CABACEstimator->split_cu_mode( split, *tempCS, partitioner );
      partitioner.modeType = modeTypeParent;
      m_CABACEstimator->mode_constraint( split, *tempCS, partitioner, modeTypeChild );
      tempCS->fracBits += m_CABACEstimator->getEstFracBits(); // split bits
    }
  }

  tempCS->cost = m_pcRdCost->calcRdCost( tempCS->fracBits, tempCS->dist );

  // Check Delta QP bits for splitted structure
  if( !qgEnableChildren ) // check at deepest QG level only
  {
    xCheckDQP(*tempCS, partitioner, true);
  }

  // If the configuration being tested exceeds the maximum number of bytes for a slice / slice-segment, then
  // a proper RD evaluation cannot be performed. Therefore, termination of the
  // slice/slice-segment must be made prior to this CTU.
  // This can be achieved by forcing the decision to be that of the rpcTempCU.
  // The exception is each slice / slice-segment must have at least one CTU.
  if (bestCS->cost != MAX_DOUBLE)
  {
  }
  else
  {
    bestCS->costDbOffset = 0;
  }
  tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();
  if( tempCS->cus.size() > 0 && modeTypeParent == MODE_TYPE_ALL && modeTypeChild == MODE_TYPE_INTER )
  {
    int areaSizeNoResiCu = 0;
    for( int k = 0; k < tempCS->cus.size(); k++ )
    {
      areaSizeNoResiCu += (tempCS->cus[k]->rootCbf == false) ? tempCS->cus[k]->lumaSize().area() : 0;
    }
    if( areaSizeNoResiCu >= (tempCS->area.lumaSize().area() >> 1) )
    {
      skipInterPass = true;
    }
  }

  splitRdCostBest[getPartSplit(encTestMode)] = tempCS->cost;
  // RD check for sub partitioned coding structure.
  xCheckBestMode( tempCS, bestCS, partitioner, encTestMode );

#if GDR_ENABLED
  if (isAffMVInfoSaved)
  {
    m_pcInterSearch->addAffMVInfo(tmpMVInfo, tmpMVInfoSolid);
  }
#else
  if (isAffMVInfoSaved)
  {
    m_pcInterSearch->addAffMVInfo(tmpMVInfo);
  }
#endif

  if (!tempCS->slice->isIntra() && isUniMvInfoSaved)
  {
    m_pcInterSearch->addUniMvInfo(tmpUniMvInfo);
  }

  tempCS->motionLut = oldMotionLut;

  tempCS->prevPLT   = oldPLT;

  tempCS->releaseIntermediateData();

  tempCS->prevQP[partitioner.chType] = oldPrevQp;
}

bool EncCu::xCheckRDCostIntra(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode, bool adaptiveColorTrans)
{
  double          bestInterCost             = m_modeCtrl->getBestInterCost();
  double          costSize2Nx2NmtsFirstPass = m_modeCtrl->getMtsSize2Nx2NFirstPassCost();
  bool            skipSecondMtsPass         = m_modeCtrl->getSkipSecondMTSPass();
  const SPS&      sps                       = *tempCS->sps;
  const int       maxSizeMTS                = MTS_INTRA_MAX_CU_SIZE;
  uint8_t         considerMtsSecondPass =
    (sps.getExplicitMtsIntraEnabled() && isLuma(partitioner.chType) && partitioner.currArea().lwidth() <= maxSizeMTS
     && partitioner.currArea().lheight() <= maxSizeMTS)
              ? 1
              : 0;

  bool   useIntraSubPartitions   = false;
  double maxCostAllowedForChroma = MAX_DOUBLE;
  const  CodingUnit *bestCU      = bestCS->getCU( partitioner.chType );
  Distortion interHad = m_modeCtrl->getInterHad();


  double dct2Cost                =   MAX_DOUBLE;
  double bestNonDCT2Cost         = MAX_DOUBLE;
  double trGrpBestCost     [ 4 ] = { MAX_DOUBLE, MAX_DOUBLE, MAX_DOUBLE, MAX_DOUBLE };
  double globalBestCost          =   MAX_DOUBLE;
  bool   bestSelFlag       [ 4 ] = { false, false, false, false };
  bool   trGrpCheck        [ 4 ] = { true, true, true, true };
  int    startMTSIdx       [ 4 ] = { 0, 1, 2, 3 };
  int    endMTSIdx         [ 4 ] = { 0, 1, 2, 3 };
  double trGrpStopThreshold[ 3 ] = { 1.001, 1.001, 1.001 };
  int    bestMtsFlag             =   0;
  int    bestLfnstIdx            =   0;

  const int  maxLfnstIdx         = (partitioner.isSepTree(*tempCS) && partitioner.chType == ChannelType::CHROMA
                           && (partitioner.currArea().lwidth() < 8 || partitioner.currArea().lheight() < 8))
                              || (partitioner.currArea().lwidth() > sps.getMaxTbSize()
                                  || partitioner.currArea().lheight() > sps.getMaxTbSize())
                                     ? 0
                                     : 2;
  bool       skipOtherLfnst      = false;
  int        startLfnstIdx       = 0;
  int        endLfnstIdx         = sps.getUseLFNST() ? maxLfnstIdx : 0;

  int grpNumMax = sps.getUseLFNST() ? m_pcEncCfg->getMTSIntraMaxCand() : 1;
  m_modeCtrl->setISPWasTested(false);
  m_pcIntraSearch->invalidateBestModeCost();
  if (sps.getUseColorTrans() && !CS::isDualITree(*tempCS))
  {
    if ((m_pcEncCfg->getRGBFormatFlag() && adaptiveColorTrans) || (!m_pcEncCfg->getRGBFormatFlag() && !adaptiveColorTrans))
    {
      m_pcIntraSearch->invalidateBestRdModeFirstColorSpace();
    }
  }

  bool foundZeroRootCbf = false;
  if (sps.getUseColorTrans())
  {
    CHECK(tempCS->treeType != TREE_D || partitioner.treeType != TREE_D, "localtree should not be applied when adaptive color transform is enabled");
    CHECK(tempCS->modeType != MODE_TYPE_ALL || partitioner.modeType != MODE_TYPE_ALL, "localtree should not be applied when adaptive color transform is enabled");
    CHECK(adaptiveColorTrans && (CS::isDualITree(*tempCS) || partitioner.chType != ChannelType::LUMA),
          "adaptive color transform cannot be applied to dual-tree");
  }

  for( int trGrpIdx = 0; trGrpIdx < grpNumMax; trGrpIdx++ )
  {
    const uint8_t startMtsFlag = trGrpIdx > 0;
    const uint8_t endMtsFlag   = sps.getUseLFNST() ? considerMtsSecondPass : 0;

    if( ( trGrpIdx == 0 || ( !skipSecondMtsPass && considerMtsSecondPass ) ) && trGrpCheck[ trGrpIdx ] )
    {
      for( int lfnstIdx = startLfnstIdx; lfnstIdx <= endLfnstIdx; lfnstIdx++ )
      {
        for( uint8_t mtsFlag = startMtsFlag; mtsFlag <= endMtsFlag; mtsFlag++ )
        {
          if (sps.getUseColorTrans() && !CS::isDualITree(*tempCS))
          {
            m_pcIntraSearch->setSavedRdModeIdx(trGrpIdx*(NUM_LFNST_NUM_PER_SET * 2) + lfnstIdx * 2 + mtsFlag);
          }
          if (mtsFlag > 0 && lfnstIdx > 0)
          {
            continue;
          }
          //3) if interHad is 0, only try further modes if some intra mode was already better than inter
          if( sps.getUseLFNST() && m_pcEncCfg->getUsePbIntraFast() && !tempCS->slice->isIntra() && bestCU && CU::isInter( *bestCS->getCU( partitioner.chType ) ) && interHad == 0 )
          {
            continue;
          }

          tempCS->initStructData( encTestMode.qp );

          CodingUnit &cu      = tempCS->addCU( CS::getArea( *tempCS, tempCS->area, partitioner.chType ), partitioner.chType );

          partitioner.setCUData( cu );
          cu.slice            = tempCS->slice;
          cu.tileIdx          = tempCS->pps->getTileIdx( tempCS->area.lumaPos() );
          cu.skip             = false;
          cu.mmvdSkip = false;
          cu.predMode         = MODE_INTRA;
          cu.chromaQpAdj      = m_cuChromaQpOffsetIdxPlus1;
          cu.qp               = encTestMode.qp;
          cu.lfnstIdx         = lfnstIdx;
          cu.mtsFlag          = mtsFlag;
          cu.ispMode          = ISPType::NONE;
          cu.colorTransform = adaptiveColorTrans;

          CU::addPUs( cu );

          tempCS->interHad    = interHad;

          m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;

          bool validCandRet = false;
          if( isLuma( partitioner.chType ) )
          {
            //ISP uses the value of the best cost so far (luma if it is the fast version) to avoid test non-necessary subpartitions
            double bestCostSoFar = partitioner.isSepTree(*tempCS)   ? m_modeCtrl->getBestCostWithoutSplitFlags()
                                   : bestCU && CU::isIntra(*bestCU) ? bestCS->lumaCost
                                                                    : bestCS->cost;
            if (partitioner.isSepTree(*tempCS) && encTestMode.maxCostAllowed < bestCostSoFar)
            {
              bestCostSoFar = encTestMode.maxCostAllowed;
            }
            validCandRet = m_pcIntraSearch->estIntraPredLumaQT(cu, partitioner, bestCostSoFar, mtsFlag, startMTSIdx[trGrpIdx], endMTSIdx[trGrpIdx], (trGrpIdx > 0), !cu.colorTransform ? bestCS : nullptr);
            if ((!validCandRet || (cu.ispMode != ISPType::NONE && cu.firstTU->cbf[COMPONENT_Y] == 0)))
            {
              continue;
            }
            if (m_pcEncCfg->getUseFastISP() && validCandRet && !mtsFlag && !lfnstIdx && !cu.colorTransform)
            {
              m_modeCtrl->setISPMode(cu.ispMode);
              m_modeCtrl->setISPLfnstIdx(cu.lfnstIdx);
              m_modeCtrl->setMIPFlagISPPass(cu.mipFlag);
              m_modeCtrl->setBestISPIntraModeRelCU(
                cu.ispMode != ISPType::NONE ? PU::getFinalIntraMode(*cu.firstPU, ChannelType::LUMA) : NOMODE_IDX);
              m_modeCtrl->setBestDCT2NonISPCostRelCU(m_modeCtrl->getMtsFirstPassNoIspCost());
            }

            if (sps.getUseColorTrans() && m_pcEncCfg->getRGBFormatFlag() && !CS::isDualITree(*tempCS) && !cu.colorTransform)
            {
              double curLumaCost = m_pcRdCost->calcRdCost(tempCS->fracBits, tempCS->dist);
              if (curLumaCost > bestCS->cost)
              {
                continue;
              }
            }

            useIntraSubPartitions = cu.ispMode != ISPType::NONE;
            if( !partitioner.isSepTree( *tempCS ) )
            {
              tempCS->lumaCost = m_pcRdCost->calcRdCost( tempCS->fracBits, tempCS->dist );
              if( useIntraSubPartitions )
              {
                //the difference between the best cost so far and the current luma cost is stored to avoid testing the Cr component if the cost of luma + Cb is larger than the best cost
                maxCostAllowedForChroma = bestCS->cost < MAX_DOUBLE ? bestCS->cost - tempCS->lumaCost : MAX_DOUBLE;
              }
            }

            if (m_pcEncCfg->getUsePbIntraFast() && tempCS->dist == std::numeric_limits<Distortion>::max()
                && tempCS->interHad == 0)
            {
              interHad = 0;
              // JEM assumes only perfect reconstructions can from now on beat the inter mode
              m_modeCtrl->enforceInterHad( 0 );
              continue;
            }

            if( !partitioner.isSepTree( *tempCS ) )
            {
              if (!cu.colorTransform)
              {
                cu.cs->picture->getRecoBuf(cu.Y()).copyFrom(cu.cs->getRecoBuf(COMPONENT_Y));
                cu.cs->picture->getPredBuf(cu.Y()).copyFrom(cu.cs->getPredBuf(COMPONENT_Y));
              }
              else
              {
                cu.cs->picture->getRecoBuf(cu).copyFrom(cu.cs->getRecoBuf(cu));
                cu.cs->picture->getPredBuf(cu).copyFrom(cu.cs->getPredBuf(cu));
              }
            }
          }

          if (tempCS->area.chromaFormat != CHROMA_400 && (partitioner.chType == ChannelType::CHROMA || !cu.isSepTree())
              && !cu.colorTransform)
          {
            TUIntraSubPartitioner subTuPartitioner( partitioner );
            m_pcIntraSearch->estIntraPredChromaQT(
              cu,
              (!useIntraSubPartitions || (cu.isSepTree() && !isLuma(ChannelType::CHROMA))) ? partitioner
                                                                                           : subTuPartitioner,
              maxCostAllowedForChroma);
            if (useIntraSubPartitions && cu.ispMode == ISPType::NONE)
            {
              //At this point the temp cost is larger than the best cost. Therefore, we can already skip the remaining calculations
              continue;
            }
          }

          cu.rootCbf = false;

          for( uint32_t t = 0; t < getNumberValidTBlocks( *cu.cs->pcv ); t++ )
          {
            cu.rootCbf |= cu.firstTU->cbf[t] != 0;
          }

          if (!cu.rootCbf)
          {
            cu.colorTransform = false;
            foundZeroRootCbf = true;
          }

          // Get total bits for current mode: encode CU
          m_CABACEstimator->resetBits();

          if ((!cu.cs->slice->isIntra() || cu.cs->slice->getSPS()->getIBCFlag())
            && cu.Y().valid()
            )
          {
            m_CABACEstimator->cu_skip_flag ( cu );
          }
          m_CABACEstimator->pred_mode      ( cu );
          m_CABACEstimator->adaptive_color_transform(cu);
          m_CABACEstimator->cu_pred_data   ( cu );

          // Encode Coefficients
          CUCtx cuCtx;
          cuCtx.isDQPCoded = true;
          cuCtx.isChromaQpAdjCoded = true;
          m_CABACEstimator->cu_residual( cu, partitioner, cuCtx );

          tempCS->fracBits = m_CABACEstimator->getEstFracBits();
          tempCS->cost     = m_pcRdCost->calcRdCost(tempCS->fracBits, tempCS->dist);


          double tmpCostWithoutSplitFlags = tempCS->cost;
          xEncodeDontSplit( *tempCS, partitioner );

          xCheckDQP( *tempCS, partitioner );
          xCheckChromaQPOffset( *tempCS, partitioner );

          // Check if low frequency non-separable transform (LFNST) is too expensive
          if (lfnstIdx && !cuCtx.lfnstLastScanPos && cu.ispMode == ISPType::NONE)
          {
            bool cbfAtZeroDepth = cu.isSepTree() ?
                                       cu.rootCbf
                                     : (tempCS->area.chromaFormat != CHROMA_400 && std::min( cu.firstTU->blocks[ 1 ].width, cu.firstTU->blocks[ 1 ].height ) < 4) ?
                                            TU::getCbfAtDepth( *cu.firstTU, COMPONENT_Y, 0 )
                                          : cu.rootCbf;
            if( cbfAtZeroDepth )
            {
              tempCS->cost = MAX_DOUBLE;
              tmpCostWithoutSplitFlags = MAX_DOUBLE;
            }
          }

          if (isLuma(partitioner.chType) && cu.firstTU->mtsIdx[COMPONENT_Y] > MtsType::SKIP)
          {
            CHECK(!cuCtx.mtsLastScanPos, "MTS is disallowed to only contain DC coefficient");
          }

          if( mtsFlag == 0 && lfnstIdx == 0 )
          {
            dct2Cost = tempCS->cost;
          }
          else if (tmpCostWithoutSplitFlags < bestNonDCT2Cost)
          {
            bestNonDCT2Cost = tmpCostWithoutSplitFlags;
          }

          if( tempCS->cost < bestCS->cost )
          {
            m_modeCtrl->setBestCostWithoutSplitFlags( tmpCostWithoutSplitFlags );
          }

          if (!mtsFlag)
          {
            static_cast<double &>(costSize2Nx2NmtsFirstPass) = tempCS->cost;
          }

          if( sps.getUseLFNST() && !tempCS->cus.empty() )
          {
            skipOtherLfnst = m_modeCtrl->checkSkipOtherLfnst( encTestMode, tempCS, partitioner );
          }

          xCalDebCost( *tempCS, partitioner );
          tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();


#if WCG_EXT
          DTRACE_MODE_COST( *tempCS, m_pcRdCost->getLambda( true ) );
#else
          DTRACE_MODE_COST( *tempCS, m_pcRdCost->getLambda() );
#endif
          if (sps.getUseColorTrans() && !CS::isDualITree(*tempCS))
          {
            int colorSpaceIdx = ((m_pcEncCfg->getRGBFormatFlag() && adaptiveColorTrans) || (!m_pcEncCfg->getRGBFormatFlag() && !adaptiveColorTrans)) ? 0 : 1;
            if (tempCS->cost < tempCS->tmpColorSpaceIntraCost[colorSpaceIdx])
            {
              tempCS->tmpColorSpaceIntraCost[colorSpaceIdx] = tempCS->cost;
              bestCS->tmpColorSpaceIntraCost[colorSpaceIdx] = tempCS->cost;
            }
          }
          if( !sps.getUseLFNST() )
          {
            xCheckBestMode( tempCS, bestCS, partitioner, encTestMode );
          }
          else
          {
            if( xCheckBestMode( tempCS, bestCS, partitioner, encTestMode ) )
            {
              trGrpBestCost[ trGrpIdx ] = globalBestCost = bestCS->cost;
              bestSelFlag  [ trGrpIdx ] = true;
              bestMtsFlag               = mtsFlag;
              bestLfnstIdx              = lfnstIdx;
              if( bestCS->cus.size() == 1 )
              {
                CodingUnit &cu = *bestCS->cus.front();
                if (cu.firstTU->mtsIdx[COMPONENT_Y] == MtsType::SKIP)
                {
                  if( ( floorLog2( cu.firstTU->blocks[ COMPONENT_Y ].width ) + floorLog2( cu.firstTU->blocks[ COMPONENT_Y ].height ) ) >= 6 )
                  {
                    endLfnstIdx = 0;
                  }
                }
              }
            }

            //we decide to skip the non-DCT-II transforms and LFNST according to the ISP results
            if ((endMtsFlag > 0 || endLfnstIdx > 0)
                && (cu.ispMode != ISPType::NONE || (bestCS && bestCS->cus[0]->ispMode != ISPType::NONE))
                && tempCS->slice->isIntra() && m_pcEncCfg->getUseFastISP())
            {
              double bestCostDct2NoIsp = m_modeCtrl->getMtsFirstPassNoIspCost();
              double bestIspCost       = m_modeCtrl->getIspCost();
              CHECKD(cu.ispMode != ISPType::NONE && bestCostDct2NoIsp <= bestIspCost, "wrong cost!");
              double threshold = 1.4;

              double lfnstThreshold = 1.01 * threshold;
              if( m_modeCtrl->getStopNonDCT2Transforms() || bestCostDct2NoIsp > bestIspCost*lfnstThreshold )
              {
                endLfnstIdx = lfnstIdx;
              }

              if ( m_modeCtrl->getStopNonDCT2Transforms() || bestCostDct2NoIsp > bestIspCost*threshold )
              {
                skipSecondMtsPass = true;
                m_modeCtrl->setSkipSecondMTSPass( true );
                break;
              }
            }
            //now we check whether the second pass of SIZE_2Nx2N and the whole Intra SIZE_NxN should be skipped or not
            if (!mtsFlag && !tempCS->slice->isIntra() && bestCU && !CU::isIntra(*bestCU))
            {
              const double thEmtInterFastSkipIntra = 1.4; // Skip checking Intra if "2Nx2N using DCT2" is worse than best Inter mode
              if( costSize2Nx2NmtsFirstPass > thEmtInterFastSkipIntra * bestInterCost )
              {
                skipSecondMtsPass = true;
                m_modeCtrl->setSkipSecondMTSPass( true );
                break;
              }
            }
          }

        } //for emtCuFlag
        if( skipOtherLfnst )
        {
          startLfnstIdx = lfnstIdx;
          endLfnstIdx   = lfnstIdx;
          break;
        }
      } //for lfnstIdx
    } //if (!skipSecondMtsPass && considerMtsSecondPass && trGrpCheck[iGrpIdx])

    if( sps.getUseLFNST() && trGrpIdx < 3 )
    {
      trGrpCheck[ trGrpIdx + 1 ] = false;

      if( bestSelFlag[ trGrpIdx ] && considerMtsSecondPass )
      {
        double dCostRatio = dct2Cost / trGrpBestCost[ trGrpIdx ];
        trGrpCheck[ trGrpIdx + 1 ] = ( bestMtsFlag != 0 || bestLfnstIdx != 0 ) && dCostRatio < trGrpStopThreshold[ trGrpIdx ];
      }
    }
  } //trGrpIdx
  if(!adaptiveColorTrans)
  {
    m_modeCtrl->setBestNonDCT2Cost(bestNonDCT2Cost);
  }
  return foundZeroRootCbf;
}


void EncCu::xCheckPLT(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode)
{
  if (((partitioner.currArea().lumaSize().width * partitioner.currArea().lumaSize().height <= 16) && (isLuma(partitioner.chType)) )
        || ((partitioner.currArea().chromaSize().width * partitioner.currArea().chromaSize().height <= 16) && (!isLuma(partitioner.chType)) && partitioner.isSepTree(*tempCS) )
      || (partitioner.isLocalSepTree(*tempCS)  && (!isLuma(partitioner.chType))  )  )
  {
    return;
  }
  tempCS->initStructData(encTestMode.qp);
  CodingUnit &cu = tempCS->addCU(CS::getArea(*tempCS, tempCS->area, partitioner.chType), partitioner.chType);
  partitioner.setCUData(cu);
  cu.slice = tempCS->slice;
  cu.tileIdx = tempCS->pps->getTileIdx(tempCS->area.lumaPos());
  cu.skip = false;
  cu.mmvdSkip = false;
  cu.predMode = MODE_PLT;

  cu.chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
  cu.qp = encTestMode.qp;
  cu.bdpcmMode   = BdpcmMode::NONE;

  tempCS->addPU(CS::getArea(*tempCS, tempCS->area, partitioner.chType), partitioner.chType);
  tempCS->addTU(CS::getArea(*tempCS, tempCS->area, partitioner.chType), partitioner.chType);
  // Search
  tempCS->dist = 0;
  if (cu.isSepTree())
  {
    if (isLuma(partitioner.chType))
    {
      m_pcIntraSearch->PLTSearch(*tempCS, partitioner, COMPONENT_Y, 1);
    }
    if (tempCS->area.chromaFormat != CHROMA_400 && (partitioner.chType == ChannelType::CHROMA))
    {
      m_pcIntraSearch->PLTSearch(*tempCS, partitioner, COMPONENT_Cb, 2);
    }
  }
  else
  {
    if( cu.chromaFormat != CHROMA_400 )
    {
      m_pcIntraSearch->PLTSearch(*tempCS, partitioner, COMPONENT_Y, 3);
    }
    else
    {
      m_pcIntraSearch->PLTSearch(*tempCS, partitioner, COMPONENT_Y, 1);
    }
  }


  m_CABACEstimator->getCtx() = m_CurrCtx->start;
  m_CABACEstimator->resetBits();
  if ((!cu.cs->slice->isIntra() || cu.cs->slice->getSPS()->getIBCFlag())
    && cu.Y().valid())
  {
    m_CABACEstimator->cu_skip_flag(cu);
  }
  m_CABACEstimator->pred_mode(cu);

  // signaling
  CUCtx cuCtx;
  cuCtx.isDQPCoded = true;
  cuCtx.isChromaQpAdjCoded = true;
  if (cu.isSepTree())
  {
    if (isLuma(partitioner.chType))
    {
      m_CABACEstimator->cu_palette_info(cu, COMPONENT_Y, 1, cuCtx);
    }
    if (tempCS->area.chromaFormat != CHROMA_400 && (partitioner.chType == ChannelType::CHROMA))
    {
      m_CABACEstimator->cu_palette_info(cu, COMPONENT_Cb, 2, cuCtx);
    }
  }
  else
  {
    if( cu.chromaFormat != CHROMA_400 )
    {
      m_CABACEstimator->cu_palette_info(cu, COMPONENT_Y, 3, cuCtx);
    }
    else
    {
      m_CABACEstimator->cu_palette_info(cu, COMPONENT_Y, 1, cuCtx);
    }
  }
  tempCS->fracBits = m_CABACEstimator->getEstFracBits();
  tempCS->cost = m_pcRdCost->calcRdCost(tempCS->fracBits, tempCS->dist);

  xEncodeDontSplit(*tempCS, partitioner);
  xCheckDQP(*tempCS, partitioner);
  xCheckChromaQPOffset( *tempCS, partitioner );
  xCalDebCost(*tempCS, partitioner);
  tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();

  const Area currCuArea = cu.block(getFirstComponentOfChannel(partitioner.chType));
  cu.slice->m_mapPltCost[isChroma(partitioner.chType)][currCuArea.pos()][currCuArea.size()] = tempCS->cost;
#if WCG_EXT
  DTRACE_MODE_COST(*tempCS, m_pcRdCost->getLambda(true));
#else
  DTRACE_MODE_COST(*tempCS, m_pcRdCost->getLambda());
#endif
  xCheckBestMode(tempCS, bestCS, partitioner, encTestMode);
}

void EncCu::xCheckDQP( CodingStructure& cs, Partitioner& partitioner, bool bKeepCtx )
{
  CHECK( bKeepCtx && cs.cus.size() <= 1 && partitioner.getImplicitSplit( cs ) == CU_DONT_SPLIT, "bKeepCtx should only be set in split case" );
  CHECK( !bKeepCtx && cs.cus.size() > 1, "bKeepCtx should never be set for non-split case" );

  if( !cs.pps->getUseDQP() )
  {
    return;
  }

  if (partitioner.isSepTree(cs) && isChroma(partitioner.chType))
  {
    return;
  }

  if( !partitioner.currQgEnable() ) // do not consider split or leaf/not leaf QG condition (checked by caller)
  {
    return;
  }


  CodingUnit* cuFirst = cs.getCU( partitioner.chType );

  CHECK( !cuFirst, "No CU available" );

  bool hasResidual = false;
  for( const auto &cu : cs.cus )
  {
    //not include the chroma CU because chroma CU is decided based on corresponding luma QP and deltaQP is not signaled at chroma CU
    if( cu->rootCbf && !isChroma( cu->chType ))
    {
      hasResidual = true;
      break;
    }
  }

  int predQP = CU::predictQP(*cuFirst, cs.prevQP[partitioner.chType]);

  if( hasResidual )
  {
    TempCtx ctxTemp(m_ctxPool);
    if (!bKeepCtx)
    {
      ctxTemp = SubCtx(Ctx::DeltaQP, m_CABACEstimator->getCtx());
    }

    m_CABACEstimator->resetBits();
    m_CABACEstimator->cu_qp_delta( *cuFirst, predQP, cuFirst->qp );

    cs.fracBits += m_CABACEstimator->getEstFracBits(); // dQP bits
    cs.cost      = m_pcRdCost->calcRdCost(cs.fracBits, cs.dist);

    if (!bKeepCtx)
    {
      m_CABACEstimator->getCtx() = SubCtx(Ctx::DeltaQP, ctxTemp);
    }

    // NOTE: reset QPs for CUs without residuals up to first coded CU
    for( const auto &cu : cs.cus )
    {
      //not include the chroma CU because chroma CU is decided based on corresponding luma QP and deltaQP is not signaled at chroma CU
      if( cu->rootCbf && !isChroma( cu->chType ))
      {
        break;
      }
      cu->qp = predQP;
    }
  }
  else
  {
    // No residuals: reset CU QP to predicted value
    for( const auto &cu : cs.cus )
    {
      cu->qp = predQP;
    }
  }
}

void EncCu::xCheckChromaQPOffset( CodingStructure& cs, Partitioner& partitioner )
{
  // doesn't apply if CU chroma QP offset is disabled
  if( !cs.slice->getUseChromaQpAdj() )
  {
    return;
  }

  // doesn't apply to luma CUs
  if( partitioner.isSepTree(cs) && isLuma(partitioner.chType) )
  {
    return;
  }

  // check cost only at cQG top-level (everything below shall not be influenced by adj coding: it occurs only once)
  if( !partitioner.currQgChromaEnable() )
  {
    return;
  }

  // check if chroma is coded or not
  bool isCoded = false;
  for( auto &cu : cs.cus )
  {
    SizeType channelWidth = !cu->isSepTree() ? cu->lwidth() : cu->chromaSize().width;
    SizeType channelHeight = !cu->isSepTree() ? cu->lheight() : cu->chromaSize().height;

    for( const TransformUnit &tu : CU::traverseTUs(*cu) )
    {
      if( tu.cbf[COMPONENT_Cb] || tu.cbf[COMPONENT_Cr] || channelWidth > 64 || channelHeight > 64)
      {
        isCoded = true;
        break;
      }
    }
    if (isCoded)
    {
      // estimate cost for coding cu_chroma_qp_offset
      TempCtx ctxTempAdjFlag(m_ctxPool);
      TempCtx ctxTempAdjIdc(m_ctxPool);
      ctxTempAdjFlag = SubCtx( Ctx::ChromaQpAdjFlag, m_CABACEstimator->getCtx() );
      ctxTempAdjIdc = SubCtx( Ctx::ChromaQpAdjIdc,   m_CABACEstimator->getCtx() );
      m_CABACEstimator->resetBits();
      m_CABACEstimator->cu_chroma_qp_offset( *cu );
      cs.fracBits += m_CABACEstimator->getEstFracBits();
      cs.cost      = m_pcRdCost->calcRdCost(cs.fracBits, cs.dist);
      m_CABACEstimator->getCtx() = SubCtx( Ctx::ChromaQpAdjFlag, ctxTempAdjFlag );
      m_CABACEstimator->getCtx() = SubCtx( Ctx::ChromaQpAdjIdc,  ctxTempAdjIdc  );
      break;
    }
    else
    {
      // chroma QP adj is forced to 0 for leading uncoded CUs
      cu->chromaQpAdj = 0;
    }
  }
}

void EncCu::xCheckRDCostHashInter( CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode )
{
  bool isPerfectMatch = false;

  tempCS->initStructData(encTestMode.qp);
  m_pcInterSearch->resetBufferedUniMotions();
  m_pcInterSearch->setAffineModeSelected(false);
  CodingUnit &cu = tempCS->addCU(tempCS->area, partitioner.chType);

  partitioner.setCUData(cu);
  cu.slice = tempCS->slice;
  cu.tileIdx = tempCS->pps->getTileIdx(tempCS->area.lumaPos());
  cu.skip = false;
  cu.predMode = MODE_INTER;
  cu.chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
  cu.qp = encTestMode.qp;
  CU::addPUs(cu);
  cu.mmvdSkip = false;
  cu.firstPU->mmvdMergeFlag = false;

  if (m_pcInterSearch->predInterHashSearch(cu, partitioner, isPerfectMatch))
  {
    double equBcwCost = MAX_DOUBLE;

    m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;

    xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, 0, 0, &equBcwCost);

    if ( m_bestModeUpdated && bestCS->cost != MAX_DOUBLE )
    {
      xCalDebCost( *bestCS, partitioner );
    }
  }
  tempCS->initStructData(encTestMode.qp);
  int minSize = min(cu.lwidth(), cu.lheight());
  if (minSize < 64)
  {
    isPerfectMatch = false;
  }
  m_modeCtrl->setIsHashPerfectMatch(isPerfectMatch);
}

int getDmvrMvdNum(const PredictionUnit& pu)
{
  int dx = std::max<int>(pu.lwidth() >> DMVR_SUBCU_WIDTH_LOG2, 1);
  int dy = std::max<int>(pu.lheight() >> DMVR_SUBCU_HEIGHT_LOG2, 1);
  return(dx * dy);
}

void EncCu::xCheckRDCostMerge2Nx2N( CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode )
{
  const Slice &slice = *tempCS->slice;

  CHECK( slice.getSliceType() == I_SLICE, "Merge modes not available for I-slices" );

  tempCS->initStructData( encTestMode.qp );

  MergeCtx mergeCtx;
  const SPS &sps = *tempCS->sps;

#if GDR_ENABLED
  bool isEncodeGdrClean = false;
  CodingStructure *cs;
#endif
  if (sps.getSbTMVPEnabledFlag())
  {
    Size bufSize = g_miScaling.scale( tempCS->area.lumaSize() );
    mergeCtx.subPuMvpMiBuf    = MotionBuf( m_SubPuMiBuf,    bufSize );
  }

  Mv   refinedMvdL0[MRG_MAX_NUM_CANDS][MAX_NUM_SUBCU_DMVR];
  setMergeBestSATDCost( MAX_DOUBLE );

  PredictionUnit* pu = getPuForInterPrediction(tempCS);
  PU::getInterMergeCandidates(*pu, mergeCtx, 0);
  PU::getInterMMVDMergeCandidates(*pu, mergeCtx);
  pu->regularMergeFlag = true;

#if GDR_ENABLED
  cs = pu->cs;
  isEncodeGdrClean = cs->sps->getGDREnabledFlag() && cs->pcv->isEncoder
    && ((cs->picHeader->getInGdrInterval() && cs->isClean(pu->Y().topRight(), ChannelType::LUMA))
      || (cs->picHeader->getNumVerVirtualBoundaries() == 0));
#endif

  bool candHasNoResidual[MRG_MAX_NUM_CANDS + MmvdIdx::ADD_NUM];
  for (uint32_t ui = 0; ui < MRG_MAX_NUM_CANDS + MmvdIdx::ADD_NUM; ui++)
  {
    candHasNoResidual[ui] = false;
  }

  bool        bestIsSkip     = false;
  bool        bestIsMMVDSkip = true;
  PelUnitBufVector<MRG_MAX_NUM_CANDS+1> rdOrderedMrgPredBuf(m_pelUnitBufPool);
  PelUnitBufVector<MRG_MAX_NUM_CANDS> mrgPredBufNoCiip(m_pelUnitBufPool);
  PelUnitBufVector<MRG_MAX_NUM_CANDS> mrgPredBufNoMvRefine(m_pelUnitBufPool);
  int         insertPos;
  const int   numDmvrMvd = getDmvrMvdNum(*pu);
  unsigned    numMergeSatdCand = mergeCtx.numValidMergeCand + MmvdIdx::ADD_NUM;

  struct ModeInfo
  {
    uint32_t mergeCand;
    bool     isRegularMerge;
    bool     isMMVD;
    bool     isCIIP;
    ModeInfo() : mergeCand(0), isRegularMerge(false), isMMVD(false), isCIIP(false) {}
    ModeInfo(const uint32_t mergeCand, const bool isRegularMerge, const bool isMMVD, const bool isCIIP) :
      mergeCand(mergeCand), isRegularMerge(isRegularMerge), isMMVD(isMMVD), isCIIP(isCIIP) {}
  };

  static_vector<ModeInfo, MRG_MAX_NUM_CANDS + MmvdIdx::ADD_NUM> rdModeList;

  const UnitArea localUnitArea(tempCS->area.chromaFormat, Area(0, 0, tempCS->area.Y().width, tempCS->area.Y().height));
  for (int i = 0; i < mergeCtx.numValidMergeCand; i++)
  {
    rdModeList.push_back(ModeInfo(i, true, false, false));
    rdOrderedMrgPredBuf.push_back(m_pelUnitBufPool.getPelUnitBuf(localUnitArea));
    mrgPredBufNoCiip.push_back(m_pelUnitBufPool.getPelUnitBuf(localUnitArea));
    mrgPredBufNoMvRefine.push_back(m_pelUnitBufPool.getPelUnitBuf(localUnitArea));
  }
  rdOrderedMrgPredBuf.push_back(m_pelUnitBufPool.getPelUnitBuf(localUnitArea));
  PelUnitBuf*& singleMergeTempBuffer = rdOrderedMrgPredBuf.back();

  if (tempCS->sps->getUseMMVD())
  {
    const int numMmvdCand = std::min<int>(MmvdIdx::BASE_MV_NUM, mergeCtx.numValidMergeCand) * MmvdIdx::MAX_REFINE_NUM;
    for (int i = 0; i < numMmvdCand; i++)
    {
      rdModeList.push_back(ModeInfo(i, false, true, false));
    }
  }

  bool mrgTempBufSet = false;

  bool isIntrainterEnabled = sps.getUseCiip();
  if (bestCS->area.lwidth() * bestCS->area.lheight() < 64 || bestCS->area.lwidth() >= MAX_CU_SIZE || bestCS->area.lheight() >= MAX_CU_SIZE)
  {
    isIntrainterEnabled = false;
  }
  bool isTestSkipMerge[MRG_MAX_NUM_CANDS]; // record if the merge candidate has tried skip mode
  for (uint32_t idx = 0; idx < MRG_MAX_NUM_CANDS; idx++)
  {
    isTestSkipMerge[idx] = false;
  }
  if( m_pcEncCfg->getUseFastMerge() || isIntrainterEnabled)
  {
    numMergeSatdCand = NUM_MRG_SATD_CAND;
    if (isIntrainterEnabled)
    {
      numMergeSatdCand += 1;
    }
    bestIsSkip       = false;

    if( auto blkCache = dynamic_cast< CacheBlkInfoCtrl* >( m_modeCtrl ) )
    {
      if (slice.getSPS()->getIBCFlag())
      {
        ComprCUCtx cuECtx = m_modeCtrl->getComprCUCtx();
        bestIsSkip = blkCache->isSkip(tempCS->area) && cuECtx.bestCU;
      }
      else
      bestIsSkip = blkCache->isSkip( tempCS->area );
      bestIsMMVDSkip = blkCache->isMMVDSkip(tempCS->area);
    }

    if (isIntrainterEnabled) // always perform low complexity check
    {
      bestIsSkip = false;
    }

    static_vector<double, MRG_MAX_NUM_CANDS + MmvdIdx::ADD_NUM> candCostList;

    // 1. Pass: get SATD-cost for selected candidates and reduce their count
    if( !bestIsSkip )
    {
      rdModeList.clear();
      mrgTempBufSet       = true;
      const TempCtx ctxStart(m_ctxPool, m_CABACEstimator->getCtx());

      const double sqrtLambdaForFirstPassIntra = m_pcRdCost->getMotionLambda( ) * FRAC_BITS_SCALE;
      partitioner.setCUData( *pu->cu );

      DistParam distParam;
      const bool bUseHadamard = !tempCS->slice->getDisableSATDForRD();
      m_pcRdCost->setDistParam(distParam, tempCS->getOrgBuf().Y(), singleMergeTempBuffer->Y(),
                               sps.getBitDepth(ChannelType::LUMA), COMPONENT_Y, bUseHadamard);

      const UnitArea localUnitArea( tempCS->area.chromaFormat, Area( 0, 0, tempCS->area.Y().width, tempCS->area.Y().height) );
      for( uint32_t uiMergeCand = 0; uiMergeCand < mergeCtx.numValidMergeCand; uiMergeCand++ )
      {
        mergeCtx.setMergeInfo( *pu, uiMergeCand );

        PU::spanMotionInfo( *pu, mergeCtx );
        pu->mvRefine = true;
        distParam.cur = singleMergeTempBuffer->Y();
        m_pcInterSearch->motionCompensation(*pu, *singleMergeTempBuffer, REF_PIC_LIST_X, true, true,
                                            mrgPredBufNoMvRefine[uiMergeCand], false);
        mrgPredBufNoCiip[uiMergeCand]->copyFrom(*singleMergeTempBuffer);
        pu->mvRefine = false;
        if (mergeCtx.interDirNeighbours[uiMergeCand] == 3)
        {
          mergeCtx.mvFieldNeighbours[uiMergeCand][0].mv = pu->mv[0];
          mergeCtx.mvFieldNeighbours[uiMergeCand][1].mv = pu->mv[1];
          if (PU::checkDMVRCondition(*pu))
          {
            std::copy_n(pu->mvdL0SubPu, numDmvrMvd, refinedMvdL0[uiMergeCand]);
          }
        }

        Distortion uiSad = distParam.distFunc(distParam);
        m_CABACEstimator->getCtx() = ctxStart;
        uint64_t fracBits = m_pcInterSearch->xCalcPuMeBits(*pu);
        double cost = (double)uiSad + (double)fracBits * sqrtLambdaForFirstPassIntra;
        insertPos = -1;

#if GDR_ENABLED
        // Non-RD cost for regular merge
        if (isEncodeGdrClean)
        {
          bool isSolid = true;
          bool isValid = true;

          for (const auto l: { REF_PIC_LIST_0, REF_PIC_LIST_1 })
          {
            const int refIdx = mergeCtx.mvFieldNeighbours[uiMergeCand][l].refIdx;

            if (refIdx >= 0)
            {
              Mv mv = mergeCtx.mvFieldNeighbours[uiMergeCand][l].mv;

              mergeCtx.mvValid[uiMergeCand][l] = cs->isClean(pu->Y().bottomRight(), mv, l, refIdx);

              isSolid &= mergeCtx.mvSolid[uiMergeCand][l];
              isValid &= mergeCtx.mvValid[uiMergeCand][l];
            }
          }

          if (!isValid || !isSolid)
          {
            cost = MAX_DOUBLE;
          }
        }
#endif
        updateCandList(ModeInfo(uiMergeCand, true, false, false), cost, rdModeList, candCostList, numMergeSatdCand,
                       &insertPos);
        if (insertPos != -1)
        {
          if (insertPos == rdModeList.size() - 1)
          {
            swap(singleMergeTempBuffer, rdOrderedMrgPredBuf[insertPos]);
          }
          else
          {
            for (uint32_t i = uint32_t(rdModeList.size()) - 1; i > insertPos; i--)
            {
              swap(rdOrderedMrgPredBuf[i - 1], rdOrderedMrgPredBuf[i]);
            }
            swap(singleMergeTempBuffer, rdOrderedMrgPredBuf[insertPos]);
          }
        }
#if !GDR_ENABLED
        CHECK(std::min(uiMergeCand + 1, numMergeSatdCand) != rdModeList.size(), "");
#endif
      }

      if (isIntrainterEnabled)
      {
        // prepare for Intra bits calculation
        pu->ciipFlag = true;

        // save the to-be-tested merge candidates
        uint32_t CiipMergeCand[NUM_MRG_SATD_CAND];
        for (uint32_t mergeCnt = 0; mergeCnt < std::min(NUM_MRG_SATD_CAND, (const int)mergeCtx.numValidMergeCand); mergeCnt++)
        {
          CiipMergeCand[mergeCnt] = rdModeList[mergeCnt].mergeCand;
        }
        for (uint32_t mergeCnt = 0; mergeCnt < std::min(std::min(NUM_MRG_SATD_CAND, (const int)mergeCtx.numValidMergeCand), 4); mergeCnt++)
        {
          uint32_t mergeCand = CiipMergeCand[mergeCnt];

          // estimate merge bits
          mergeCtx.setMergeInfo(*pu, mergeCand);

          // first round
          pu->intraDir[ChannelType::LUMA] = PLANAR_IDX;
          uint32_t intraCnt = 0;
          // generate intrainter Y prediction
          if (mergeCnt == 0)
          {
            m_pcIntraSearch->initIntraPatternChType(*pu->cu, pu->Y());
            m_pcIntraSearch->predIntraAng(COMPONENT_Y, pu->cs->getPredBuf(*pu).Y(), *pu);
            m_pcIntraSearch->switchBuffer(*pu, COMPONENT_Y, pu->cs->getPredBuf(*pu).Y(), m_pcIntraSearch->getPredictorPtr2(COMPONENT_Y, intraCnt));
          }
          pu->cs->getPredBuf(*pu).copyFrom(*mrgPredBufNoMvRefine[mergeCand]);
          if (pu->cs->slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())
          {
            pu->cs->getPredBuf(*pu).Y().rspSignal(m_pcReshape->getFwdLUT());
          }
          m_pcIntraSearch->geneWeightedPred(pu->cs->getPredBuf(*pu).Y(), *pu,
                                            m_pcIntraSearch->getPredictorPtr2(COMPONENT_Y, intraCnt));

          // calculate cost
          if (pu->cs->slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())
          {
            pu->cs->getPredBuf(*pu).Y().rspSignal(m_pcReshape->getInvLUT());
          }
          distParam.cur = pu->cs->getPredBuf(*pu).Y();
          Distortion sadValue = distParam.distFunc(distParam);
          if (pu->cs->slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())
          {
            pu->cs->getPredBuf(*pu).Y().rspSignal(m_pcReshape->getFwdLUT());
          }
          m_CABACEstimator->getCtx() = ctxStart;
          pu->regularMergeFlag = false;
          uint64_t fracBits = m_pcInterSearch->xCalcPuMeBits(*pu);
          double cost = (double)sadValue + (double)fracBits * sqrtLambdaForFirstPassIntra;
#if GDR_ENABLED
          // Non-RD cost for CIIP merge
          if (isEncodeGdrClean)
          {
            bool isSolid = true;
            bool isValid = true;

            for (const auto l: { REF_PIC_LIST_0, REF_PIC_LIST_1 })
            {
              if (mergeCtx.mvFieldNeighbours[mergeCand][l].refIdx >= 0)
              {
                Mv  mv     = mergeCtx.mvFieldNeighbours[mergeCand][l].mv;
                int refIdx = mergeCtx.mvFieldNeighbours[mergeCand][l].refIdx;

                mergeCtx.mvValid[mergeCand][l] = cs->isClean(pu->Y().bottomRight(), mv, l, refIdx);

                isSolid &= mergeCtx.mvSolid[mergeCand][l];
                isValid &= mergeCtx.mvValid[mergeCand][l];
              }
            }

            if (!isValid || !isSolid)
            {
              cost = MAX_DOUBLE;
            }
          }
#endif

          insertPos = -1;
          updateCandList(ModeInfo(mergeCand, false, false, true), cost, rdModeList, candCostList, numMergeSatdCand,
                         &insertPos);
          if (insertPos != -1)
          {
            for (int i = int(rdModeList.size()) - 1; i > insertPos; i--)
            {
              swap(rdOrderedMrgPredBuf[i - 1], rdOrderedMrgPredBuf[i]);
            }
            swap(singleMergeTempBuffer, rdOrderedMrgPredBuf[insertPos]);
          }
        }
        pu->ciipFlag = false;
      }
      if ( pu->cs->sps->getUseMMVD() )
      {
        pu->cu->mmvdSkip = true;
        pu->regularMergeFlag = true;
        const int tempNum   = (mergeCtx.numValidMergeCand > 1) ? MmvdIdx::ADD_NUM : MmvdIdx::ADD_NUM >> 1;
        for (int mmvdMergeCand = 0; mmvdMergeCand < tempNum; mmvdMergeCand++)
        {
          MmvdIdx mmvdIdx;
          mmvdIdx.val = mmvdMergeCand;

          if (mmvdIdx.pos.step >= m_pcEncCfg->getMmvdDisNum())
          {
            continue;
          }
#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            pu->mvSolid[REF_PIC_LIST_0] = true;
            pu->mvSolid[REF_PIC_LIST_1] = true;

            pu->mvValid[REF_PIC_LIST_0] = true;
            pu->mvValid[REF_PIC_LIST_1] = true;
          }
#endif
          mergeCtx.setMmvdMergeCandiInfo(*pu, mmvdIdx);

          PU::spanMotionInfo(*pu, mergeCtx);
          pu->mvRefine = true;
          distParam.cur = singleMergeTempBuffer->Y();
          pu->mmvdEncOptMode = (mmvdIdx.pos.step > 2 ? 2 : 1);
          CHECK(!pu->mmvdMergeFlag, "MMVD merge should be set");
          // Don't do chroma MC here
          m_pcInterSearch->motionCompensation(*pu, *singleMergeTempBuffer, REF_PIC_LIST_X, true, false, nullptr, false);
          pu->mmvdEncOptMode = 0;
          pu->mvRefine = false;
          Distortion uiSad = distParam.distFunc(distParam);

          m_CABACEstimator->getCtx() = ctxStart;
          uint64_t fracBits = m_pcInterSearch->xCalcPuMeBits(*pu);
          double cost = (double)uiSad + (double)fracBits * sqrtLambdaForFirstPassIntra;
          insertPos = -1;

#if GDR_ENABLED
          if (isEncodeGdrClean)
          {
            bool isSolid = true;
            bool isValid = true;

            if (pu->refIdx[0] >= 0)
            {
              isSolid = isSolid && pu->mvSolid[0];
              isValid = isValid && pu->mvValid[0];
            }

            if (pu->refIdx[1] >= 0)
            {
              isSolid = isSolid && pu->mvSolid[1];
              isValid = isValid && pu->mvValid[1];
            }

            if (!isSolid || !isValid)
            {
              cost = MAX_DOUBLE;
            }
          }
#endif
          updateCandList(ModeInfo(mmvdMergeCand, false, true, false), cost, rdModeList, candCostList, numMergeSatdCand,
                         &insertPos);
          if (insertPos != -1)
          {
            for (int i = int(rdModeList.size()) - 1; i > insertPos; i--)
            {
              swap(rdOrderedMrgPredBuf[i - 1], rdOrderedMrgPredBuf[i]);
            }
            swap(singleMergeTempBuffer, rdOrderedMrgPredBuf[insertPos]);
          }
        }
      }
      // Try to limit number of candidates using SATD-costs
      numMergeSatdCand = updateRdCheckingNum(MRG_FAST_RATIO * candCostList[0], numMergeSatdCand, candCostList);

      setMergeBestSATDCost( candCostList[0] );

      if (isIntrainterEnabled && isChromaEnabled(pu->cs->pcv->chrFormat))
      {
        pu->ciipFlag = true;
        for (uint32_t mergeCnt = 0; mergeCnt < numMergeSatdCand; mergeCnt++)
        {
          if (rdModeList[mergeCnt].isCIIP)
          {
            pu->intraDir[ChannelType::LUMA]   = PLANAR_IDX;
            pu->intraDir[ChannelType::CHROMA] = DM_CHROMA_IDX;
            if (pu->chromaSize().width == 2)
            {
              continue;
            }
            uint32_t bufIdx = 0;
            m_pcIntraSearch->initIntraPatternChType(*pu->cu, pu->Cb());
            m_pcIntraSearch->predIntraAng(COMPONENT_Cb, pu->cs->getPredBuf(*pu).Cb(), *pu);
            m_pcIntraSearch->switchBuffer(*pu, COMPONENT_Cb, pu->cs->getPredBuf(*pu).Cb(), m_pcIntraSearch->getPredictorPtr2(COMPONENT_Cb, bufIdx));

            m_pcIntraSearch->initIntraPatternChType(*pu->cu, pu->Cr());
            m_pcIntraSearch->predIntraAng(COMPONENT_Cr, pu->cs->getPredBuf(*pu).Cr(), *pu);
            m_pcIntraSearch->switchBuffer(*pu, COMPONENT_Cr, pu->cs->getPredBuf(*pu).Cr(), m_pcIntraSearch->getPredictorPtr2(COMPONENT_Cr, bufIdx));
          }
        }
        pu->ciipFlag = false;
      }

      tempCS->initStructData( encTestMode.qp );
      m_CABACEstimator->getCtx() = ctxStart;
    }
    else
    {
      if (bestIsMMVDSkip)
      {
        numMergeSatdCand =
          mergeCtx.numValidMergeCand + ((mergeCtx.numValidMergeCand > 1) ? MmvdIdx::ADD_NUM : MmvdIdx::ADD_NUM >> 1);
      }
      else
      {
        numMergeSatdCand = mergeCtx.numValidMergeCand;
      }
    }
  }
  m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;
  uint32_t iteration;
  uint32_t iterationBegin = 0;
  iteration = 2;
  for (uint32_t noResidualPass = iterationBegin; noResidualPass < iteration; ++noResidualPass)
  {
    for (uint32_t mrgHadIdx = 0; mrgHadIdx < numMergeSatdCand; mrgHadIdx++)
    {
      uint32_t uiMergeCand = rdModeList[mrgHadIdx].mergeCand;

      if (noResidualPass != 0 && rdModeList[mrgHadIdx].isCIIP)   // intrainter does not support skip mode
      {
        if (isTestSkipMerge[uiMergeCand])
        {
          continue;
        }
      }

      if (((noResidualPass != 0) && candHasNoResidual[mrgHadIdx]) || ((noResidualPass == 0) && bestIsSkip))
      {
        continue;
      }

      // first get merge candidates
      pu = getPuForInterPrediction(tempCS);
      partitioner.setCUData(*pu->cu);

      if (noResidualPass == 0 && rdModeList[mrgHadIdx].isCIIP)
      {
        pu->cu->mmvdSkip = false;
        mergeCtx.setMergeInfo(*pu, uiMergeCand);
        pu->ciipFlag = true;
        pu->regularMergeFlag = false;
        pu->intraDir[ChannelType::LUMA] = PLANAR_IDX;
        CHECK(pu->intraDir[ChannelType::LUMA] < 0 || pu->intraDir[ChannelType::LUMA] > (NUM_LUMA_MODE - 1),
              "out of intra mode");
        pu->intraDir[ChannelType::CHROMA] = DM_CHROMA_IDX;
      }
      else if (rdModeList[mrgHadIdx].isMMVD)
      {
        pu->cu->mmvdSkip = true;
        pu->regularMergeFlag = true;
        MmvdIdx mmvdIdx;
        mmvdIdx.val = uiMergeCand;
        mergeCtx.setMmvdMergeCandiInfo(*pu, mmvdIdx);
      }
      else
      {
        pu->cu->mmvdSkip = false;
        pu->regularMergeFlag = true;
        mergeCtx.setMergeInfo(*pu, uiMergeCand);
      }
      PU::spanMotionInfo( *pu, mergeCtx );

      if( m_pcEncCfg->getMCTSEncConstraint() )
      {
        bool isDMVR = PU::checkDMVRCondition( *pu );
        if( ( isDMVR && MCTSHelper::isRefBlockAtRestrictedTileBoundary( *pu ) ) || ( !isDMVR && !( MCTSHelper::checkMvBufferForMCTSConstraint( *pu ) ) ) )
        {
          // Do not use this mode
          tempCS->initStructData( encTestMode.qp );
          continue;
        }
      }
      if( mrgTempBufSet )
      {
        if (PU::checkDMVRCondition(*pu))
        {
          std::copy_n(refinedMvdL0[uiMergeCand], numDmvrMvd, pu->mvdL0SubPu);
        }
        if (pu->ciipFlag)
        {
          uint32_t bufIdx = 0;
          PelBuf tmpBuf = tempCS->getPredBuf(*pu).Y();
          tmpBuf.copyFrom(mrgPredBufNoMvRefine[uiMergeCand]->Y());
          if (pu->cs->slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())
          {
            tmpBuf.rspSignal(m_pcReshape->getFwdLUT());
          }
          m_pcIntraSearch->geneWeightedPred(tmpBuf, *pu, m_pcIntraSearch->getPredictorPtr2(COMPONENT_Y, bufIdx));
          if (isChromaEnabled(pu->chromaFormat))
          {
            if (pu->chromaSize().width > 2)
            {
              tmpBuf = tempCS->getPredBuf(*pu).Cb();
              tmpBuf.copyFrom(mrgPredBufNoMvRefine[uiMergeCand]->Cb());
              m_pcIntraSearch->geneWeightedPred(tmpBuf, *pu, m_pcIntraSearch->getPredictorPtr2(COMPONENT_Cb, bufIdx));
              tmpBuf = tempCS->getPredBuf(*pu).Cr();
              tmpBuf.copyFrom(mrgPredBufNoMvRefine[uiMergeCand]->Cr());
              m_pcIntraSearch->geneWeightedPred(tmpBuf, *pu, m_pcIntraSearch->getPredictorPtr2(COMPONENT_Cr, bufIdx));
            }
            else
            {
              tmpBuf = tempCS->getPredBuf(*pu).Cb();
              tmpBuf.copyFrom(mrgPredBufNoMvRefine[uiMergeCand]->Cb());
              tmpBuf = tempCS->getPredBuf(*pu).Cr();
              tmpBuf.copyFrom(mrgPredBufNoMvRefine[uiMergeCand]->Cr());
            }
          }
        }
        else
        {
          if (rdModeList[mrgHadIdx].isMMVD)
          {
            pu->mmvdEncOptMode = 0;
            m_pcInterSearch->motionCompensatePu(*pu, REF_PIC_LIST_X, true, true);
          }
          else if (noResidualPass != 0 && rdModeList[mrgHadIdx].isCIIP)
          {
            tempCS->getPredBuf().copyFrom(*mrgPredBufNoCiip[uiMergeCand]);
          }
          else
          {
            tempCS->getPredBuf().copyFrom(*rdOrderedMrgPredBuf[mrgHadIdx]);
          }
        }
      }
      else
      {
        pu->mvRefine = true;
        m_pcInterSearch->motionCompensatePu(*pu, REF_PIC_LIST_X, true, true);
        pu->mvRefine = false;
      }
      if (!pu->cu->mmvdSkip && !pu->ciipFlag && noResidualPass != 0)
      {
        CHECK(uiMergeCand >= mergeCtx.numValidMergeCand, "out of normal merge");
        isTestSkipMerge[uiMergeCand] = true;
      }

#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        bool isSolid = true;
        bool isValid = true;

        if (pu->refIdx[0] >= 0)
        {
          isSolid = isSolid && pu->mvSolid[0];
          isValid = isValid && pu->mvValid[0];
        }

        if (pu->refIdx[1] >= 0)
        {
          isSolid = isSolid && pu->mvSolid[1];
          isValid = isValid && pu->mvValid[1];
        }

        if (isSolid && isValid)
        {
          xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, noResidualPass,
                               noResidualPass == 0 ? &candHasNoResidual[mrgHadIdx] : nullptr);
        }
      }
      else
      {
        xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, noResidualPass,
                             noResidualPass == 0 ? &candHasNoResidual[mrgHadIdx] : nullptr);
      }
#else
      xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, noResidualPass,
                           noResidualPass == 0 ? &candHasNoResidual[mrgHadIdx] : nullptr);
#endif

      if( m_pcEncCfg->getUseFastDecisionForMerge() && !bestIsSkip && !pu->ciipFlag)
      {
        bestIsSkip = !bestCS->cus.empty() && bestCS->getCU( partitioner.chType )->rootCbf == 0;
      }
      tempCS->initStructData( encTestMode.qp );
    }   // end loop mrgHadIdx

    if (noResidualPass == 0 && m_pcEncCfg->getUseEarlySkipDetection())
    {
      checkEarlySkip(bestCS, partitioner);
    }
  }
  if ( m_bestModeUpdated && bestCS->cost != MAX_DOUBLE )
  {
    xCalDebCost( *bestCS, partitioner );
  }
}

void EncCu::xCheckRDCostMergeGeo2Nx2N(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &pm, const EncTestMode& encTestMode)
{
  const Slice &slice = *tempCS->slice;
  CHECK(slice.getSliceType() == I_SLICE, "Merge modes not available for I-slices");

  tempCS->initStructData(encTestMode.qp);

  MergeCtx mergeCtx;
  const SPS &sps = *tempCS->sps;

  if (sps.getSbTMVPEnabledFlag())
  {
    Size bufSize = g_miScaling.scale(tempCS->area.lumaSize());
    mergeCtx.subPuMvpMiBuf = MotionBuf(m_SubPuMiBuf, bufSize);
  }

  PredictionUnit* pu = getPuForInterPrediction(tempCS);
  pm.setCUData(*pu->cu);

#if GDR_ENABLED
  CodingStructure &cs = *pu->cs;
  const bool       isEncodeGdrClean =
    cs.sps->getGDREnabledFlag() && cs.pcv->isEncoder
    && ((cs.picHeader->getInGdrInterval() && cs.isClean(pu->Y().topRight(), ChannelType::LUMA))
        || (cs.picHeader->getNumVerVirtualBoundaries() == 0));
#endif

  pu->mergeFlag = true;
  pu->regularMergeFlag = false;
  pu->cu->geoFlag = true;
  PU::getGeoMergeCandidates(*pu, mergeCtx);

  const int bitsForPartitionIdx = floorLog2(GEO_NUM_PARTITION_MODE);
  PelUnitBufVector<MRG_MAX_NUM_CANDS> geoBuffer(m_pelUnitBufPool);
  PelUnitBufVector<MRG_MAX_NUM_CANDS> geoTempBuf(m_pelUnitBufPool);
  DistParam distParam;

  const UnitArea localUnitArea(tempCS->area.chromaFormat, Area(0, 0, tempCS->area.Y().width, tempCS->area.Y().height));
  const double sqrtLambdaForFirstPass = m_pcRdCost->getMotionLambda();
  uint8_t maxNumMergeCandidates = pu->cs->sps->getMaxNumGeoCand();
  DistParam distParamWholeBlk;
  // the third arguments to setDistParam is dummy and will be updated before being used
  m_pcRdCost->setDistParam(distParamWholeBlk, tempCS->getOrgBuf().Y(), tempCS->getOrgBuf().Y(),
                           sps.getBitDepth(ChannelType::LUMA), COMPONENT_Y);
  Distortion bestWholeBlkSad = MAX_UINT64;
  double bestWholeBlkCost = MAX_DOUBLE;
  Distortion sadWholeBlk[GEO_MAX_NUM_UNI_CANDS];
  int        pocMrg[GEO_MAX_NUM_UNI_CANDS];
  Mv         mergeMv[GEO_MAX_NUM_UNI_CANDS];
  bool       isSkipThisCand[GEO_MAX_NUM_UNI_CANDS];

  for (int i = 0; i < maxNumMergeCandidates; i++)
  {
    isSkipThisCand[i] = false;
  }
  for (uint8_t mergeCand = 0; mergeCand < maxNumMergeCandidates; mergeCand++)
  {
    geoBuffer.push_back(m_pelUnitBufPool.getPelUnitBuf(localUnitArea));
    geoTempBuf.push_back(m_pelUnitBufPool.getPelUnitBuf(localUnitArea));
    mergeCtx.setMergeInfo(*pu, mergeCand);

    const int  listIdx    = mergeCtx.mvFieldNeighbours[mergeCand][0].refIdx == -1 ? 1 : 0;
    const auto refPicList = RefPicList(listIdx);
    const int  refIdx     = mergeCtx.mvFieldNeighbours[mergeCand][listIdx].refIdx;

    pocMrg[mergeCand]  = tempCS->slice->getRefPic(refPicList, refIdx)->getPOC();
    mergeMv[mergeCand] = mergeCtx.mvFieldNeighbours[mergeCand][listIdx].mv;

    for (int i = 0; i < mergeCand; i++)
    {
      if (pocMrg[mergeCand] == pocMrg[i] && mergeMv[mergeCand] == mergeMv[i])
      {
        isSkipThisCand[mergeCand] = true;
        break;
      }
    }

    PU::spanMotionInfo(*pu, mergeCtx);
    if (m_pcEncCfg->getMCTSEncConstraint() && (!(MCTSHelper::checkMvBufferForMCTSConstraint(*pu))))
    {
      tempCS->initStructData(encTestMode.qp);
      return;
    }
    m_pcInterSearch->motionCompensation(*pu, *geoBuffer[mergeCand], REF_PIC_LIST_X);
    geoTempBuf[mergeCand]->Y().copyFrom(geoBuffer[mergeCand]->Y());
    geoTempBuf[mergeCand]->Y().roundToOutputBitdepth(geoTempBuf[mergeCand]->Y(), pu->cs->slice->clpRng(COMPONENT_Y));
    distParamWholeBlk.cur = geoTempBuf[mergeCand]->Y();
    sadWholeBlk[mergeCand] = distParamWholeBlk.distFunc(distParamWholeBlk);
#if GDR_ENABLED
    bool allOk = (sadWholeBlk[mergeCand] < bestWholeBlkSad);
    if (isEncodeGdrClean)
    {
      bool isSolid = mergeCtx.mvSolid[mergeCand][listIdx];
      bool isValid = mergeCtx.mvValid[mergeCand][listIdx];
      allOk = allOk && isSolid && isValid;
    }
#endif
#if GDR_ENABLED
    if (allOk)
#else
    if (sadWholeBlk[mergeCand] < bestWholeBlkSad)
#endif
    {
      bestWholeBlkSad = sadWholeBlk[mergeCand];
      int bitsCand = mergeCand + 1;
      bestWholeBlkCost = (double)bestWholeBlkSad + (double)bitsCand * sqrtLambdaForFirstPass;
    }
  }

  bool allCandsAreSame = true;
  for (uint8_t mergeCand = 1; mergeCand < maxNumMergeCandidates; mergeCand++)
  {
    allCandsAreSame &= isSkipThisCand[mergeCand];
  }
  if (allCandsAreSame)
  {
    return;
  }

  const int wIdx = floorLog2(pu->lwidth()) - GEO_MIN_CU_LOG2;
  const int hIdx = floorLog2(pu->lheight()) - GEO_MIN_CU_LOG2;

  for (int splitDir = 0; splitDir < GEO_NUM_PARTITION_MODE; splitDir++)
  {
    int maskStride = 0, maskStride2 = 0;
    int stepX = 1;
    Pel    *sadMask;
    int16_t angle = g_GeoParams[splitDir][0];
    if (g_angle2mirror[angle] == 2)
    {
      maskStride = -GEO_WEIGHT_MASK_SIZE;
      maskStride2 = -(int)pu->lwidth();
      sadMask     = &g_globalGeoEncSADmask[g_angle2mask[g_GeoParams[splitDir][0]]]
                                      [(GEO_WEIGHT_MASK_SIZE - 1 - g_weightOffset[splitDir][hIdx][wIdx][1])
                                         * GEO_WEIGHT_MASK_SIZE
                                       + g_weightOffset[splitDir][hIdx][wIdx][0]];
    }
    else if (g_angle2mirror[angle] == 1)
    {
      stepX = -1;
      maskStride2 = pu->lwidth();
      maskStride = GEO_WEIGHT_MASK_SIZE;
      sadMask     = &g_globalGeoEncSADmask[g_angle2mask[g_GeoParams[splitDir][0]]]
                                      [g_weightOffset[splitDir][hIdx][wIdx][1] * GEO_WEIGHT_MASK_SIZE
                                       + (GEO_WEIGHT_MASK_SIZE - 1 - g_weightOffset[splitDir][hIdx][wIdx][0])];
    }
    else
    {
      maskStride = GEO_WEIGHT_MASK_SIZE;
      maskStride2 = -(int)pu->lwidth();
      sadMask     = &g_globalGeoEncSADmask[g_angle2mask[g_GeoParams[splitDir][0]]]
                                      [g_weightOffset[splitDir][hIdx][wIdx][1] * GEO_WEIGHT_MASK_SIZE
                                       + g_weightOffset[splitDir][hIdx][wIdx][0]];
    }

    for (uint8_t mergeCand = 0; mergeCand < maxNumMergeCandidates; mergeCand++)
    {
      m_pcRdCost->setDistParam(distParam, tempCS->getOrgBuf().Y(), geoTempBuf[mergeCand]->Y().buf,
                               geoTempBuf[mergeCand]->Y().stride, sadMask, maskStride, stepX, maskStride2,
                               sps.getBitDepth(ChannelType::LUMA), COMPONENT_Y);
      const Distortion sadLarge = distParam.distFunc(distParam);
      const Distortion sadSmall = sadWholeBlk[mergeCand] - sadLarge;

      const int bitsCand = mergeCand + 1;

      const double cost0 = (double) sadLarge + (double) bitsCand * sqrtLambdaForFirstPass;
      const double cost1 = (double) sadSmall + (double) bitsCand * sqrtLambdaForFirstPass;

      m_geoCostList.insert(splitDir, 0, mergeCand, cost0);
      m_geoCostList.insert(splitDir, 1, mergeCand, cost1);
    }
  }

  GeoComboCostList &comboList = m_comboList;
  comboList.list.clear();

  for (int geoMotionIdx = 0; geoMotionIdx < maxNumMergeCandidates * (maxNumMergeCandidates - 1); geoMotionIdx++)
  {
    const MergeIdxPair mergeIdxPair = m_geoModeTest[geoMotionIdx];

#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        if (!mergeCtx.mvSolid[mergeIdxPair[0]][0] || !mergeCtx.mvSolid[mergeIdxPair[0]][1] || !mergeCtx.mvSolid[mergeIdxPair[1]][0]
            || !mergeCtx.mvSolid[mergeIdxPair[1]][1] || !mergeCtx.mvValid[mergeIdxPair[0]][0] || !mergeCtx.mvValid[mergeIdxPair[0]][1]
            || !mergeCtx.mvValid[mergeIdxPair[1]][0] || !mergeCtx.mvValid[mergeIdxPair[1]][1])
        {
          // don't insert candidate into comboList so we don't have to test for cleanliness later
          continue;
        }
      }
#endif

      for (int splitDir = 0; splitDir < GEO_NUM_PARTITION_MODE; splitDir++)
      {
        double tempCost = m_geoCostList.getCost(splitDir, mergeIdxPair);

        if (tempCost > bestWholeBlkCost)
        {
          continue;
        }

        tempCost = tempCost + (double) bitsForPartitionIdx * sqrtLambdaForFirstPass;
        comboList.list.push_back(GeoMergeCombo(splitDir, mergeIdxPair, tempCost));
      }
  }
  if (comboList.list.empty())
  {
    return;
  }
  comboList.sortByCost();

  bool bestIsSkip = false;

  static_vector<uint8_t, GEO_MAX_TRY_WEIGHTED_SAD> geoRdModeList;
  static_vector<double, GEO_MAX_TRY_WEIGHTED_SAD> geocandCostList;

  DistParam distParamSAD2;
  const bool useHadamard = !tempCS->slice->getDisableSATDForRD();
  // the third arguments to setDistParam is dummy and will be updated before being used
  m_pcRdCost->setDistParam(distParamSAD2, tempCS->getOrgBuf().Y(), tempCS->getOrgBuf().Y(),
                           sps.getBitDepth(ChannelType::LUMA), COMPONENT_Y, useHadamard);

  const int geoNumMrgSadCand  = min(GEO_MAX_TRY_WEIGHTED_SAD, (int) comboList.list.size());
  int geoNumMrgSatdCand = min(GEO_MAX_TRY_WEIGHTED_SATD, (int) comboList.list.size());

  for (int candidateIdx = 0; candidateIdx < geoNumMrgSadCand; candidateIdx++)
  {
    const int splitDir   = comboList.list[candidateIdx].splitDir;
    const int mergeCand0 = comboList.list[candidateIdx].mergeIdx[0];
    const int mergeCand1 = comboList.list[candidateIdx].mergeIdx[1];

    PelUnitBuf geoBuf = m_geoWeightedBuffers[candidateIdx].getBuf(localUnitArea);
    m_pcInterSearch->weightedGeoBlk(*pu, splitDir, ChannelType::LUMA, geoBuf, *geoBuffer[mergeCand0],
                                    *geoBuffer[mergeCand1]);
    distParamSAD2.cur = geoBuf.Y();
    Distortion sad    = distParamSAD2.distFunc(distParamSAD2);

    int mvBits = 0;
    mvBits += 1 + mergeCand0;
    mvBits += 1 + mergeCand1 - (mergeCand1 < mergeCand0 ? 0 : 1);

    const double updateCost = (double) sad + (double) (bitsForPartitionIdx + mvBits) * sqrtLambdaForFirstPass;

    comboList.list[candidateIdx].cost = updateCost;
    updateCandList((uint8_t) candidateIdx, updateCost, geoRdModeList, geocandCostList, geoNumMrgSatdCand);
  }

  const double threshold = std::min(geocandCostList[0] * MRG_FAST_RATIO,
    std::min(getMergeBestSATDCost(), getAFFBestSATDCost()));
  geoNumMrgSatdCand = updateRdCheckingNum(threshold, geoNumMrgSatdCand, geocandCostList);

  if (isChromaEnabled(pu->chromaFormat))
  {
    // Generate chroma predictions
    for (int i = 0; i < geoNumMrgSatdCand; i++)
    {
      const int          candidateIdx = geoRdModeList[i];
      const int          splitDir     = comboList.list[candidateIdx].splitDir;
      const MergeIdxPair mergeCand    = comboList.list[candidateIdx].mergeIdx;

      PelUnitBuf geoBuf = m_geoWeightedBuffers[candidateIdx].getBuf(localUnitArea);
      m_pcInterSearch->weightedGeoBlk(*pu, splitDir, ChannelType::CHROMA, geoBuf, *geoBuffer[mergeCand[0]],
                                      *geoBuffer[mergeCand[1]]);
    }
  }

  std::array<bool, GEO_MAX_TRY_WEIGHTED_SAD> geocandHasNoResidual;
  geocandHasNoResidual.fill(false);

  m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;
  tempCS->initStructData(encTestMode.qp);
  uint8_t iteration      = 2;
  uint8_t iterationBegin = 0;

  for (uint8_t noResidualPass = iterationBegin; noResidualPass < iteration; ++noResidualPass)
  {
    for (uint8_t mrgHADIdx = 0; mrgHADIdx < geoNumMrgSatdCand; mrgHADIdx++)
    {
      uint8_t candidateIdx = geoRdModeList[mrgHADIdx];
      if (noResidualPass != 0 ? geocandHasNoResidual[candidateIdx] : bestIsSkip)
      {
        continue;
      }
      pu = getPuForInterPrediction(tempCS);
      pm.setCUData(*pu->cu);
      pu->mergeFlag        = true;
      pu->regularMergeFlag = false;
      pu->geoSplitDir      = comboList.list[candidateIdx].splitDir;
      pu->geoMergeIdx0     = (uint8_t) comboList.list[candidateIdx].mergeIdx0;
      pu->geoMergeIdx1     = (uint8_t) comboList.list[candidateIdx].mergeIdx1;
      pu->mmvdMergeFlag    = false;
      pu->mmvdMergeIdx.val = MmvdIdx::INVALID;
      pu->cu->geoFlag      = true;

      PU::spanGeoMotionInfo(*pu, mergeCtx, pu->geoSplitDir, pu->geoMergeIdx);
      PelUnitBuf geoBuf = m_geoWeightedBuffers[candidateIdx].getBuf(localUnitArea);
      tempCS->getPredBuf().copyFrom(geoBuf);

      xEncodeInterResidual(tempCS, bestCS, pm, encTestMode, noResidualPass,
                           (noResidualPass == 0 ? &geocandHasNoResidual[candidateIdx] : nullptr));

      if (m_pcEncCfg->getUseFastDecisionForMerge() && !bestIsSkip)
      {
        bestIsSkip = bestCS->getCU(pm.chType)->rootCbf == 0;
      }
      tempCS->initStructData(encTestMode.qp);
    }
  }
  if (m_bestModeUpdated && bestCS->cost != MAX_DOUBLE)
  {
    xCalDebCost(*bestCS, pm);
  }
}

void EncCu::xCheckRDCostAffineMerge2Nx2N( CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode )
{
  if( m_modeCtrl->getFastDeltaQp() )
  {
    return;
  }

  if ( bestCS->area.lumaSize().width < 8 || bestCS->area.lumaSize().height < 8 )
  {
    return;
  }
#if GDR_ENABLED
  CodingStructure *cs;
  bool isEncodeGdrClean = false;
#endif
  m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;
  const Slice &slice = *tempCS->slice;

  CHECK( slice.getSliceType() == I_SLICE, "Affine Merge modes not available for I-slices" );

  tempCS->initStructData( encTestMode.qp );

  AffineMergeCtx affineMergeCtx;
  const SPS &sps = *tempCS->sps;
  if (sps.getMaxNumAffineMergeCand() == 0)
  {
    return;
  }

  setAFFBestSATDCost(MAX_DOUBLE);

  MergeCtx mrgCtx;
  if (sps.getSbTMVPEnabledFlag())
  {
    Size bufSize = g_miScaling.scale( tempCS->area.lumaSize() );
    mrgCtx.subPuMvpMiBuf = MotionBuf( m_SubPuMiBuf, bufSize );
    affineMergeCtx.mrgCtx = &mrgCtx;
  }

  PredictionUnit* pu = getPuForInterPrediction(tempCS);
  partitioner.setCUData(*pu->cu);
  pu->regularMergeFlag = false;
  pu->cu->affine = true;
#if GDR_ENABLED
  cs = pu->cs;
  isEncodeGdrClean = cs->sps->getGDREnabledFlag() && cs->pcv->isEncoder
    && ((cs->picHeader->getInGdrInterval() && cs->isClean(pu->Y().topRight(), ChannelType::LUMA))
      || (cs->picHeader->getNumVerVirtualBoundaries() == 0));
#endif
  PU::getAffineMergeCand(*pu, affineMergeCtx);

  if (affineMergeCtx.numValidMergeCand <= 0)
  {
    return;
  }

  bool candHasNoResidual[AFFINE_MRG_MAX_NUM_CANDS];
  for ( uint32_t ui = 0; ui < affineMergeCtx.numValidMergeCand; ui++ )
  {
    candHasNoResidual[ui] = false;
  }

  bool       bestIsSkip       = false;
  bool       mrgTempBufSet    = false;
  uint32_t   numMergeSatdCand = affineMergeCtx.numValidMergeCand;
  PelUnitBufVector<AFFINE_MRG_MAX_NUM_CANDS> mrgPredBuf(m_pelUnitBufPool);

  static_vector<uint32_t, AFFINE_MRG_MAX_NUM_CANDS> rdModeList;

  for ( uint32_t i = 0; i < AFFINE_MRG_MAX_NUM_CANDS; i++ )
  {
    rdModeList.push_back(i);
  }

  if ( m_pcEncCfg->getUseFastMerge() )
  {
    numMergeSatdCand = std::min(NUM_AFF_MRG_SATD_CAND, affineMergeCtx.numValidMergeCand);
    bestIsSkip = false;

    if ( auto blkCache = dynamic_cast<CacheBlkInfoCtrl*>(m_modeCtrl) )
    {
      bestIsSkip = blkCache->isSkip( tempCS->area );
    }

    static_vector<double, AFFINE_MRG_MAX_NUM_CANDS> candCostList;

    // 1. Pass: get SATD-cost for selected candidates and reduce their count
    if ( !bestIsSkip )
    {
      rdModeList.clear();
      mrgTempBufSet = true;
      const double sqrtLambdaForFirstPass = m_pcRdCost->getMotionLambda( );

      DistParam distParam;
      const bool bUseHadamard = !tempCS->slice->getDisableSATDForRD();
      // the third arguments to setDistParam is dummy and will be updated before being used
      m_pcRdCost->setDistParam(distParam, tempCS->getOrgBuf().Y(), tempCS->getOrgBuf().Y(),
                               sps.getBitDepth(ChannelType::LUMA), COMPONENT_Y, bUseHadamard);

      const UnitArea localUnitArea( tempCS->area.chromaFormat, Area( 0, 0, tempCS->area.Y().width, tempCS->area.Y().height ) );

      for ( uint32_t uiMergeCand = 0; uiMergeCand < affineMergeCtx.numValidMergeCand; uiMergeCand++ )
      {
        mrgPredBuf.push_back(m_pelUnitBufPool.getPelUnitBuf(localUnitArea));

        // set merge information
        pu->interDir         = affineMergeCtx.interDirNeighbours[uiMergeCand];
        pu->mergeFlag        = true;
        pu->regularMergeFlag = false;
        pu->mergeIdx         = uiMergeCand;
        pu->cu->affineType   = affineMergeCtx.affineType[uiMergeCand];
        pu->cu->bcwIdx       = affineMergeCtx.bcwIdx[uiMergeCand];

        pu->mergeType = affineMergeCtx.mergeType[uiMergeCand];
        if (pu->mergeType == MergeType::SUBPU_ATMVP)
        {
          pu->refIdx[0] = affineMergeCtx.mvFieldNeighbours[uiMergeCand][0][0].refIdx;
          pu->refIdx[1] = affineMergeCtx.mvFieldNeighbours[uiMergeCand][0][1].refIdx;
          PU::spanMotionInfo( *pu, mrgCtx );
        }
        else
        {
          PU::setAllAffineMvField(*pu, affineMergeCtx.mvFieldNeighbours[uiMergeCand], REF_PIC_LIST_0);
          PU::setAllAffineMvField(*pu, affineMergeCtx.mvFieldNeighbours[uiMergeCand], REF_PIC_LIST_1);

          PU::spanMotionInfo( *pu );
        }

#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          Mv zero = Mv(0, 0);
          const bool isValid = cs->isSubPuClean(*pu, &zero);
          for (auto &c: affineMergeCtx.mvValid[uiMergeCand])
          {
            c[0] = isValid;
            c[1] = isValid;
          }
        }
#endif
        distParam.cur = mrgPredBuf[uiMergeCand]->Y();

        m_pcInterSearch->motionCompensation(*pu, *mrgPredBuf[uiMergeCand], REF_PIC_LIST_X, true, false, nullptr, false);

        Distortion uiSad = distParam.distFunc( distParam );
        uint32_t   uiBitsCand = uiMergeCand + 1;
        if ( uiMergeCand == tempCS->picHeader->getMaxNumAffineMergeCand() - 1 )
        {
          uiBitsCand--;
        }
        double cost = (double)uiSad + (double)uiBitsCand * sqrtLambdaForFirstPass;
#if GDR_ENABLED
        if (isEncodeGdrClean)
        {
          bool isSolid0 = affineMergeCtx.isSolid(uiMergeCand, REF_PIC_LIST_0);
          bool isSolid1 = affineMergeCtx.isSolid(uiMergeCand, REF_PIC_LIST_1);
          bool isValid0 = affineMergeCtx.isValid(uiMergeCand, REF_PIC_LIST_0);
          bool isValid1 = affineMergeCtx.isValid(uiMergeCand, REF_PIC_LIST_1);

          if (!isSolid0 || !isSolid1 || !isValid0 || !isValid1)
          {
            cost = MAX_DOUBLE;
          }
        }
#endif
        updateCandList(uiMergeCand, cost, rdModeList, candCostList, numMergeSatdCand);

        CHECK(std::min(uiMergeCand + 1, numMergeSatdCand) != rdModeList.size(), "");
      }

      // Try to limit number of candidates using SATD-costs
      numMergeSatdCand = updateRdCheckingNum(MRG_FAST_RATIO * candCostList[0], numMergeSatdCand, candCostList);

      tempCS->initStructData( encTestMode.qp );
      setAFFBestSATDCost(candCostList[0]);

    }
    else
    {
      numMergeSatdCand = affineMergeCtx.numValidMergeCand;
    }
  }

  uint32_t iteration;
  uint32_t iterationBegin = 0;
  iteration = 2;
  for (uint32_t noResidualPass = iterationBegin; noResidualPass < iteration; ++noResidualPass)
  {
    for (uint32_t mrgHadIdx = 0; mrgHadIdx < numMergeSatdCand; mrgHadIdx++)
    {
      uint32_t uiMergeCand = rdModeList[mrgHadIdx];

      if (((noResidualPass != 0) && candHasNoResidual[uiMergeCand]) || ((noResidualPass == 0) && bestIsSkip))
      {
        continue;
      }

      // first get merge candidates
      pu = getPuForInterPrediction(tempCS);
      partitioner.setCUData(*pu->cu);

      // set merge information
      pu->mergeFlag = true;
      pu->mergeIdx = uiMergeCand;
      pu->interDir = affineMergeCtx.interDirNeighbours[uiMergeCand];
      pu->cu->affineType = affineMergeCtx.affineType[uiMergeCand];
      pu->cu->bcwIdx     = affineMergeCtx.bcwIdx[uiMergeCand];
      pu->cu->affine = true;
      pu->mergeType = affineMergeCtx.mergeType[uiMergeCand];
      if (pu->mergeType == MergeType::SUBPU_ATMVP)
      {
        pu->refIdx[0] = affineMergeCtx.mvFieldNeighbours[uiMergeCand][0][0].refIdx;
        pu->refIdx[1] = affineMergeCtx.mvFieldNeighbours[uiMergeCand][0][1].refIdx;
        PU::spanMotionInfo( *pu, mrgCtx );
      }
      else
      {
        PU::setAllAffineMvField(*pu, affineMergeCtx.mvFieldNeighbours[uiMergeCand], REF_PIC_LIST_0);
        PU::setAllAffineMvField(*pu, affineMergeCtx.mvFieldNeighbours[uiMergeCand], REF_PIC_LIST_1);

        PU::spanMotionInfo( *pu );
      }

      if( m_pcEncCfg->getMCTSEncConstraint() && ( !( MCTSHelper::checkMvBufferForMCTSConstraint( *pu ) ) ) )
      {
        // Do not use this mode
        tempCS->initStructData( encTestMode.qp );
        return;
      }
      if ( mrgTempBufSet )
      {
        tempCS->getPredBuf().copyFrom(*mrgPredBuf[uiMergeCand], true, false);   // Copy Luma Only
        m_pcInterSearch->motionCompensatePu(*pu, REF_PIC_LIST_X, false, true);
      }
      else
      {
        m_pcInterSearch->motionCompensatePu(*pu, REF_PIC_LIST_X, true, true);
      }

#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        if (bestIsSkip)
        {
          Mv zero = Mv(0, 0);
          const bool isValid = cs->isSubPuClean(*pu, &zero);
          for (auto &c: affineMergeCtx.mvValid[uiMergeCand])
          {
            c[0] = isValid;
            c[1] = isValid;
          }
        }

        const bool isSolid0 = affineMergeCtx.isSolid(uiMergeCand, REF_PIC_LIST_0);
        const bool isSolid1 = affineMergeCtx.isSolid(uiMergeCand, REF_PIC_LIST_1);
        const bool isValid0 = affineMergeCtx.isValid(uiMergeCand, REF_PIC_LIST_0);
        const bool isValid1 = affineMergeCtx.isValid(uiMergeCand, REF_PIC_LIST_1);

        if (isSolid0 && isSolid1 && isValid0 && isValid1)
        {
          xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, noResidualPass,
                               (noResidualPass == 0 ? &candHasNoResidual[uiMergeCand] : nullptr));
        }
      }
      else
      {
        xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, noResidualPass,
                             (noResidualPass == 0 ? &candHasNoResidual[uiMergeCand] : nullptr));
      }
#else
      xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, noResidualPass,
                           (noResidualPass == 0 ? &candHasNoResidual[uiMergeCand] : nullptr));
#endif

      if ( m_pcEncCfg->getUseFastDecisionForMerge() && !bestIsSkip )
      {
#if GDR_ENABLED
        if (bestCS->getCU(partitioner.chType))
        {
          bestIsSkip = bestCS->getCU(partitioner.chType)->rootCbf == 0;
        }
        else
        {
          bestIsSkip = false;
        }
#else
        bestIsSkip = bestCS->getCU(partitioner.chType)->rootCbf == 0;
#endif
      }
      tempCS->initStructData( encTestMode.qp );
    }   // end loop mrgHadIdx

    if (noResidualPass == 0 && m_pcEncCfg->getUseEarlySkipDetection())
    {
      checkEarlySkip(bestCS, partitioner);
    }
  }
  if ( m_bestModeUpdated && bestCS->cost != MAX_DOUBLE )
  {
    xCalDebCost( *bestCS, partitioner );
  }
}
//////////////////////////////////////////////////////////////////////////////////////////////
// ibc merge/skip mode check
void EncCu::xCheckRDCostIBCModeMerge2Nx2N(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode)
{
  CHECK(partitioner.chType == ChannelType::CHROMA, "chroma IBC is derived");

  // don't use IBC for large CUs
  if (tempCS->area.lwidth() > IBC_MAX_CU_SIZE || tempCS->area.lheight() > IBC_MAX_CU_SIZE)
  {
    return;
  }

  const SPS &sps = *tempCS->sps;

  tempCS->initStructData(encTestMode.qp);
  MergeCtx mergeCtx;

  if (sps.getSbTMVPEnabledFlag())
  {
    Size bufSize = g_miScaling.scale(tempCS->area.lumaSize());
    mergeCtx.subPuMvpMiBuf = MotionBuf(m_SubPuMiBuf, bufSize);
  }

#if GDR_ENABLED
  bool gdrClean = true;
#endif
  {
    // first get merge candidates
    CodingUnit cu(tempCS->area);
    cu.cs = tempCS;
    cu.predMode = MODE_IBC;
    cu.slice = tempCS->slice;
    cu.tileIdx          = tempCS->pps->getTileIdx( tempCS->area.lumaPos() );
    PredictionUnit pu(tempCS->area);
    pu.cu = &cu;
    pu.cs = tempCS;
    cu.mmvdSkip = false;
    pu.mmvdMergeFlag = false;
    pu.regularMergeFlag = false;
    cu.geoFlag = false;
    PU::getIBCMergeCandidates(pu, mergeCtx);
#if GDR_ENABLED
    gdrClean = tempCS->isClean(pu.Y().topRight(), ChannelType::LUMA);
#endif
  }
#if GDR_ENABLED
  const bool isEncodeGdrClean = tempCS->sps->getGDREnabledFlag() && tempCS->pcv->isEncoder && tempCS->picHeader->getInGdrInterval() && gdrClean;
  bool *MrgSolid = nullptr;
  bool *MrgValid = nullptr;
#endif

  int candHasNoResidual[MRG_MAX_NUM_CANDS];
  for (unsigned int ui = 0; ui < mergeCtx.numValidMergeCand; ui++)
  {
    candHasNoResidual[ui] = 0;
  }

#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    MrgSolid = new bool[MRG_MAX_NUM_CANDS];
    MrgValid = new bool[MRG_MAX_NUM_CANDS];
    for (int i = 0; i < MRG_MAX_NUM_CANDS; i++)
    {
      MrgSolid[i] = false;
      MrgValid[i] = false;
    }
  }
#endif

  bool                                        bestIsSkip = false;
  unsigned                                    numMrgSATDCand = mergeCtx.numValidMergeCand;
  static_vector<unsigned, MRG_MAX_NUM_CANDS>  rdModeList(MRG_MAX_NUM_CANDS);
  for (unsigned i = 0; i < MRG_MAX_NUM_CANDS; i++)
  {
    rdModeList[i] = i;
  }

  static_vector<double, MRG_MAX_NUM_CANDS> candCostList(MRG_MAX_NUM_CANDS, MAX_DOUBLE);
  // 1. Pass: get SATD-cost for selected candidates and reduce their count
  {
    const double sqrtLambdaForFirstPass = m_pcRdCost->getMotionLambda();

    CodingUnit &cu = tempCS->addCU(CS::getArea(*tempCS, tempCS->area, (const ChannelType) partitioner.chType),
                                   (const ChannelType) partitioner.chType);

    partitioner.setCUData(cu);
    cu.slice       = tempCS->slice;
    cu.tileIdx     = tempCS->pps->getTileIdx(tempCS->area.lumaPos());
    cu.skip        = false;
    cu.predMode    = MODE_IBC;
    cu.chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
    cu.qp          = encTestMode.qp;
    cu.mmvdSkip    = false;
    cu.geoFlag     = false;
    DistParam       distParam;
    const bool      bUseHadamard = !cu.slice->getDisableSATDForRD();
    PredictionUnit &pu           = tempCS->addPU(cu, partitioner.chType);   // tempCS->addPU(cu);
    pu.mmvdMergeFlag             = false;
    pu.regularMergeFlag          = false;
    Picture *     refPic         = pu.cu->slice->getPic();
    const CPelBuf refBuf         = refPic->getRecoBuf(pu.blocks[COMPONENT_Y]);
    const Pel *   piRefSrch      = refBuf.buf;
    if (tempCS->slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())
    {
      const CompArea &area = cu.blocks[COMPONENT_Y];
      CompArea        tmpArea(COMPONENT_Y, area.chromaFormat, Position(0, 0), area.size());
      PelBuf          tmpLuma = m_tmpStorageLCU->getBuf(tmpArea);
      tmpLuma.copyFrom(tempCS->getOrgBuf().Y());
      tmpLuma.rspSignal(m_pcReshape->getFwdLUT());
      m_pcRdCost->setDistParam(distParam, tmpLuma, refBuf, sps.getBitDepth(ChannelType::LUMA), COMPONENT_Y,
                               bUseHadamard);
    }
    else
    {
      m_pcRdCost->setDistParam(distParam, tempCS->getOrgBuf().Y(), refBuf, sps.getBitDepth(ChannelType::LUMA),
                               COMPONENT_Y, bUseHadamard);
    }
    ptrdiff_t      refStride = refBuf.stride;
    const UnitArea localUnitArea(tempCS->area.chromaFormat,
                                 Area(0, 0, tempCS->area.Y().width, tempCS->area.Y().height));
    int            numValidBv = mergeCtx.numValidMergeCand;
    for (unsigned int mergeCand = 0; mergeCand < mergeCtx.numValidMergeCand; mergeCand++)
    {
      mergeCtx.setMergeInfo(pu, mergeCand);   // set bv info in merge mode
      const int          cuPelX    = pu.Y().x;
      const int          cuPelY    = pu.Y().y;
      int                roiWidth  = pu.lwidth();
      int                roiHeight = pu.lheight();
      const int          picWidth  = pu.cs->slice->getPPS()->getPicWidthInLumaSamples();
      const int          picHeight = pu.cs->slice->getPPS()->getPicHeightInLumaSamples();
      const unsigned int lcuWidth  = pu.cs->slice->getSPS()->getMaxCUWidth();
      int                xPred     = pu.bv.getHor();
      int                yPred     = pu.bv.getVer();

      if (!m_pcInterSearch->searchBv(pu, cuPelX, cuPelY, roiWidth, roiHeight, picWidth, picHeight, xPred, yPred,
                                     lcuWidth))   // not valid bv derived
      {
        numValidBv--;
        continue;
      }
      PU::spanMotionInfo(pu, mergeCtx);

      distParam.cur.buf = piRefSrch + refStride * yPred + xPred;

      Distortion   sad      = distParam.distFunc(distParam);
      unsigned int bitsCand = mergeCand + 1;
      if (mergeCand == tempCS->sps->getMaxNumMergeCand() - 1)
      {
        bitsCand--;
      }
      double cost = (double) sad + (double) bitsCand * sqrtLambdaForFirstPass;
#if GDR_ENABLED
      if (isEncodeGdrClean)
      {
        bool isSolid = true;
        bool isValid = true;

        for (const auto l: { REF_PIC_LIST_0, REF_PIC_LIST_1 })
        {
          if (mergeCtx.mvFieldNeighbours[mergeCand][l].refIdx >= 0)
          {
            Mv  mv   = mergeCtx.mvFieldNeighbours[mergeCand][l].mv;
            int ridx = mergeCtx.mvFieldNeighbours[mergeCand][l].refIdx;

            mergeCtx.mvValid[mergeCand][l] = tempCS->isClean(pu.Y().bottomRight(), mv, l, ridx, true);

            isSolid &= mergeCtx.mvSolid[mergeCand][l];
            isValid &= mergeCtx.mvValid[mergeCand][l];
          }
        }

        if (!isValid || !isSolid)
        {
          cost = MAX_DOUBLE;
          numValidBv--;
        }
      }
#endif
      updateCandList(mergeCand, cost, rdModeList, candCostList, numMrgSATDCand);
    }

    // Try to limit number of candidates using SATD-costs
    if (numValidBv)
    {
      numMrgSATDCand = numValidBv;
      for (unsigned int i = 1; i < numValidBv; i++)
      {
        if (candCostList[i] > MRG_FAST_RATIO * candCostList[0])
        {
          numMrgSATDCand = i;
          break;
        }
      }
    }
    else
    {
      tempCS->dist         = 0;
      tempCS->fracBits     = 0;
      tempCS->cost         = MAX_DOUBLE;
      tempCS->costDbOffset = 0;
      tempCS->initStructData(encTestMode.qp);
      return;
    }

    tempCS->initStructData(encTestMode.qp);
  }

  const unsigned int iteration = 2;
  m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;
  // 2. Pass: check candidates using full RD test
  for (unsigned int numResidualPass = 0; numResidualPass < iteration; numResidualPass++)
  {
    for (unsigned int mrgHADIdx = 0; mrgHADIdx < numMrgSATDCand; mrgHADIdx++)
    {
      unsigned int mergeCand = rdModeList[mrgHADIdx];
      if (!(numResidualPass == 1 && candHasNoResidual[mergeCand] == 1))
      {
        if (!(bestIsSkip && (numResidualPass == 0)))
        {
          {
            // first get merge candidates
            CodingUnit &cu = tempCS->addCU(CS::getArea(*tempCS, tempCS->area, (const ChannelType)partitioner.chType), (const ChannelType)partitioner.chType);

            partitioner.setCUData(cu);
            cu.slice = tempCS->slice;
            cu.tileIdx = tempCS->pps->getTileIdx( tempCS->area.lumaPos() );
            cu.skip = false;
            cu.predMode = MODE_IBC;
            cu.chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
            cu.qp = encTestMode.qp;
            cu.sbtInfo = 0;

            PredictionUnit &pu = tempCS->addPU(cu, partitioner.chType);// tempCS->addPU(cu);
            pu.intraDir[ChannelType::LUMA]   = DC_IDX;                               // set intra pred for ibc block
            pu.intraDir[ChannelType::CHROMA] = PLANAR_IDX;                           // set intra pred for ibc block
            cu.mmvdSkip = false;
            pu.mmvdMergeFlag = false;
            pu.regularMergeFlag = false;
            cu.geoFlag = false;
            mergeCtx.setMergeInfo(pu, mergeCand);
            PU::spanMotionInfo(pu, mergeCtx);

            const bool chroma = !pu.cu->isSepTree();
#if GDR_ENABLED
            // redo validation again for Skip
            {
              CodingStructure &cs = *pu.cs;

              if (isEncodeGdrClean)
              {
                for (const auto l: { REF_PIC_LIST_0, REF_PIC_LIST_1 })
                {
                  if (mergeCtx.mvFieldNeighbours[mergeCand][l].refIdx >= 0)
                  {
                    Mv  mv     = mergeCtx.mvFieldNeighbours[mergeCand][l].mv;
                    int refIdx = mergeCtx.mvFieldNeighbours[mergeCand][l].refIdx;

                    mergeCtx.mvValid[mergeCand][l] = cs.isClean(pu.Y().bottomRight(), mv, l, refIdx, true);
                  }
                }
              }
            }
#endif
            //  MC
            m_pcInterSearch->motionCompensatePu(pu, REF_PIC_LIST_0, true, chroma);
            m_CABACEstimator->getCtx() = m_CurrCtx->start;

#if GDR_ENABLED
            if (isEncodeGdrClean)
            {
              bool mvSolid = true;
              bool mvValid = true;
              for (const auto l: { REF_PIC_LIST_0, REF_PIC_LIST_1 })
              {
                if (mergeCtx.mvFieldNeighbours[mergeCand][l].refIdx >= 0)
                {
                  mvSolid &= mergeCtx.mvSolid[mergeCand][l];
                  mvValid &= mergeCtx.mvValid[mergeCand][l];
                }
              }

              if (mvSolid && mvValid)
              {
                m_pcInterSearch->encodeResAndCalcRdInterCU(*tempCS, partitioner, (numResidualPass != 0), true, chroma);
              }
            }
            else
            {
              m_pcInterSearch->encodeResAndCalcRdInterCU(*tempCS, partitioner, (numResidualPass != 0), true, chroma);
            }
#else
            m_pcInterSearch->encodeResAndCalcRdInterCU(*tempCS, partitioner, (numResidualPass != 0), true, chroma);
#endif
            if (tempCS->slice->getSPS()->getUseColorTrans())
            {
              bestCS->tmpColorSpaceCost = tempCS->tmpColorSpaceCost;
              bestCS->firstColorSpaceSelected = tempCS->firstColorSpaceSelected;
            }
            xEncodeDontSplit(*tempCS, partitioner);

#if ENABLE_QPA_SUB_CTU
            xCheckDQP (*tempCS, partitioner);
#else
            // this if-check is redundant
            if (tempCS->pps->getUseDQP() && partitioner.currQgEnable())
            {
              xCheckDQP(*tempCS, partitioner);
            }
#endif
            xCheckChromaQPOffset( *tempCS, partitioner );


            DTRACE_MODE_COST(*tempCS, m_pcRdCost->getLambda());
            xCheckBestMode(tempCS, bestCS, partitioner, encTestMode);

            tempCS->initStructData(encTestMode.qp);
          }

          if (m_pcEncCfg->getUseFastDecisionForMerge() && !bestIsSkip)
          {
            if (bestCS->getCU(partitioner.chType) == nullptr)
            {
              bestIsSkip = 0;
            }
            else
            {
              bestIsSkip = bestCS->getCU(partitioner.chType)->rootCbf == 0;
            }
          }
        }
      }
    }
  }
  if ( m_bestModeUpdated && bestCS->cost != MAX_DOUBLE )
  {
    xCalDebCost( *bestCS, partitioner );
  }
#if GDR_ENABLED
  if (isEncodeGdrClean)
  {
    delete[] MrgSolid;
    delete[] MrgValid;
  }
#endif
}

PredictionUnit* EncCu::getPuForInterPrediction(CodingStructure* cs)
{
  PredictionUnit* pu = cs->getPU(ChannelType::LUMA);
  if (pu == nullptr)
  {
    CHECK(cs->getCU(ChannelType::LUMA) != nullptr, "Wrong CU/PU setting in CS");
    CodingUnit &cu = cs->addCU(cs->area, ChannelType::LUMA);
    pu = &cs->addPU(cu, ChannelType::LUMA);
  }
  pu->cu->slice = cs->slice;
  pu->cu->tileIdx = cs->pps->getTileIdx(cs->area.lumaPos());
  pu->cu->skip = false;
  pu->cu->mmvdSkip = false;
  pu->cu->geoFlag = false;
  pu->cu->predMode = MODE_INTER;
  pu->cu->chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
  pu->cu->qp = cs->currQP[ChannelType::LUMA];

  return pu;
}

template<size_t N>
unsigned int EncCu::updateRdCheckingNum(double threshold, unsigned int numMergeSatdCand, static_vector<double, N>& costList)
{
  for (uint32_t i = 0; i < numMergeSatdCand; i++)
  {
    if (costList[i] > threshold)
    {
      numMergeSatdCand = i;
      break;
    }
  }
  return numMergeSatdCand;
}

void EncCu::checkEarlySkip(const CodingStructure* bestCS, const Partitioner &partitioner)
{
  const CodingUnit     &bestCU = *bestCS->getCU(partitioner.chType);
  const PredictionUnit &bestPU = *bestCS->getPU(partitioner.chType);

  if (bestCU.rootCbf == 0)
  {
    if (bestPU.mergeFlag)
    {
      m_modeCtrl->setEarlySkipDetected();
    }
    else if (m_pcEncCfg->getMotionEstimationSearchMethod() != MESearchMethod::SELECTIVE)
    {
      int mvdAbsSum = 0;

      for (auto l : { REF_PIC_LIST_0, REF_PIC_LIST_1 })
      {
        if (bestCS->slice->getNumRefIdx(l) > 0)
        {
          mvdAbsSum += bestPU.mvd[l].getAbsHor() + bestPU.mvd[l].getAbsVer();
        }
      }

      if (mvdAbsSum == 0)
      {
        m_modeCtrl->setEarlySkipDetected();
      }
    }
  }
}

void EncCu::xCheckRDCostIBCMode(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode)
{
  if (tempCS->area.lwidth() > IBC_MAX_CU_SIZE || tempCS->area.lheight() > IBC_MAX_CU_SIZE)
  {
    // disable IBC mode larger than 64x64
    return;
  }

  tempCS->initStructData(encTestMode.qp);

  m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;

  CodingUnit &cu = tempCS->addCU(CS::getArea(*tempCS, tempCS->area, partitioner.chType), partitioner.chType);

  partitioner.setCUData(cu);
  cu.slice       = tempCS->slice;
  cu.tileIdx     = tempCS->pps->getTileIdx(tempCS->area.lumaPos());
  cu.skip        = false;
  cu.predMode    = MODE_IBC;
  cu.chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
  cu.qp          = encTestMode.qp;
  cu.imv         = 0;
  cu.sbtInfo     = 0;

  CU::addPUs(cu);

  m_bestModeUpdated = tempCS->useDbCost = bestCS->useDbCost = false;

  PredictionUnit &pu  = *cu.firstPU;
  cu.mmvdSkip         = false;
  pu.mmvdMergeFlag    = false;
  pu.regularMergeFlag = false;

  pu.intraDir[ChannelType::LUMA]   = DC_IDX;       // set intra pred for ibc block
  pu.intraDir[ChannelType::CHROMA] = PLANAR_IDX;   // set intra pred for ibc block

  pu.interDir               = 1;             // use list 0 for IBC mode
  pu.refIdx[REF_PIC_LIST_0] = IBC_REF_IDX;   // last idx in the list
  bool bValid =
    m_pcInterSearch->predIBCSearch(cu, partitioner, m_ctuIbcSearchRangeX, m_ctuIbcSearchRangeY, m_ibcHashMap);

  if (bValid)
  {
    PU::spanMotionInfo(pu);
    const bool chroma = !pu.cu->isSepTree();
    //  MC
    m_pcInterSearch->motionCompensatePu(pu, REF_PIC_LIST_0, true, chroma);

    m_pcInterSearch->encodeResAndCalcRdInterCU(*tempCS, partitioner, false, true, chroma);
    if (tempCS->slice->getSPS()->getUseColorTrans())
    {
      bestCS->tmpColorSpaceCost       = tempCS->tmpColorSpaceCost;
      bestCS->firstColorSpaceSelected = tempCS->firstColorSpaceSelected;
    }

    xEncodeDontSplit(*tempCS, partitioner);

#if ENABLE_QPA_SUB_CTU
    xCheckDQP(*tempCS, partitioner);
#else
    // this if-check is redundant
    if (tempCS->pps->getUseDQP() && partitioner.currQgEnable())
    {
      xCheckDQP(*tempCS, partitioner);
    }
#endif
    xCheckChromaQPOffset(*tempCS, partitioner);

    tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();
    if (m_bestModeUpdated)
    {
      xCalDebCost(*tempCS, partitioner);
    }

    DTRACE_MODE_COST(*tempCS, m_pcRdCost->getLambda());
    xCheckBestMode(tempCS, bestCS, partitioner, encTestMode);
  }   // bValid
  else
  {
    tempCS->dist         = 0;
    tempCS->fracBits     = 0;
    tempCS->cost         = MAX_DOUBLE;
    tempCS->costDbOffset = 0;
  }
}
  // check ibc mode in encoder RD
  //////////////////////////////////////////////////////////////////////////////////////////////

void EncCu::xCheckRDCostInter( CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner, const EncTestMode& encTestMode )
{
  m_pcInterSearch->setAffineModeSelected(false);

  m_pcInterSearch->resetBufferedUniMotions();

  const int bcwLoopNum = tempCS->slice->isInterB() && tempCS->sps->getUseBcw()
                             && tempCS->area.lwidth() * tempCS->area.lheight() >= BCW_SIZE_CONSTRAINT
                           ? BCW_NUM
                           : 1;

  double curBestCost = bestCS->cost;
  double equBcwCost = MAX_DOUBLE;

  m_bestModeUpdated = false;
  tempCS->useDbCost = false;
  bestCS->useDbCost = false;

  for( int bcwLoopIdx = 0; bcwLoopIdx < bcwLoopNum; bcwLoopIdx++ )
  {
    if( m_pcEncCfg->getUseBcwFast() )
    {
      const auto blkCache = dynamic_cast<CacheBlkInfoCtrl *>(m_modeCtrl);

      if( blkCache )
      {
        const bool    isBestInter = blkCache->getInter(bestCS->area);
        const uint8_t bestBcwIdx  = blkCache->getBcwIdx(bestCS->area);

        if( isBestInter && g_BcwSearchOrder[bcwLoopIdx] != BCW_DEFAULT && g_BcwSearchOrder[bcwLoopIdx] != bestBcwIdx )
        {
          continue;
        }
      }
    }
    if( !tempCS->slice->getCheckLDC() )
    {
      if( bcwLoopIdx != 0 && bcwLoopIdx != 3 && bcwLoopIdx != 4 )
      {
        continue;
      }
    }

    tempCS->initStructData(encTestMode.qp);

    CodingUnit &cu = tempCS->addCU(tempCS->area, partitioner.chType);

    partitioner.setCUData(cu);
    cu.slice       = tempCS->slice;
    cu.tileIdx     = tempCS->pps->getTileIdx(tempCS->area.lumaPos());
    cu.skip        = false;
    cu.mmvdSkip    = false;
    cu.predMode    = MODE_INTER;
    cu.chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
    cu.qp          = encTestMode.qp;
    CU::addPUs(cu);

    cu.bcwIdx       = g_BcwSearchOrder[bcwLoopIdx];
    uint8_t bcwIdx  = cu.bcwIdx;
    bool    testBcw = bcwIdx != BCW_DEFAULT;

#if GDR_ENABLED
    const bool isEncodeGdrClean =
      tempCS->sps->getGDREnabledFlag() && tempCS->pcv->isEncoder
      && ((tempCS->picHeader->getInGdrInterval() && tempCS->isClean(cu.Y().topRight(), ChannelType::LUMA))
          || (tempCS->picHeader->getNumVerVirtualBoundaries() == 0));
#endif
    m_pcInterSearch->predInterSearch(cu, partitioner);

    bcwIdx = CU::getValidBcwIdx(cu);
    if (testBcw && bcwIdx == BCW_DEFAULT)   // Enabled BCW but the search results is uni
    {
      continue;
    }
    CHECK(!testBcw && bcwIdx != BCW_DEFAULT, "Bad BCW index");

#if GDR_ENABLED
    // 2.0 xCheckRDCostInter: check residual (compare with bestCS)
    bool isClean = true;
    if (isEncodeGdrClean)
    {
      if (cu.affine && cu.firstPU)
      {
        bool L0ok = true, L1ok = true, L3ok = true;

        L0ok = L0ok && cu.firstPU->mvAffiSolid[0][0] && cu.firstPU->mvAffiSolid[0][1] && cu.firstPU->mvAffiSolid[0][2];
        L0ok = L0ok && cu.firstPU->mvAffiValid[0][0] && cu.firstPU->mvAffiValid[0][1] && cu.firstPU->mvAffiValid[0][2];

        L1ok = L1ok && cu.firstPU->mvAffiSolid[1][0] && cu.firstPU->mvAffiSolid[1][1] && cu.firstPU->mvAffiSolid[1][2];
        L1ok = L1ok && cu.firstPU->mvAffiValid[1][0] && cu.firstPU->mvAffiValid[1][1] && cu.firstPU->mvAffiValid[1][2];

        L3ok = L0ok && L1ok;

        if (cu.firstPU->interDir == 1 && !L0ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 2 && !L1ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 3 && !L3ok)
        {
          isClean = false;
        }
      }
      else if (cu.firstPU)
      {
        bool L0ok = true;
        bool L1ok = true;
        bool L3ok = true;

        L0ok = L0ok && cu.firstPU->mvSolid[0];
        L0ok = L0ok && cu.firstPU->mvValid[0];

        L1ok = L1ok && cu.firstPU->mvSolid[1];
        L1ok = L1ok && cu.firstPU->mvValid[1];

        L3ok = L0ok && L1ok;

        if (cu.firstPU->interDir == 1 && !L0ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 2 && !L1ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 3 && !L3ok)
        {
          isClean = false;
        }
      }
      else
      {
        isClean = false;
      }
    }

    if (isClean)
#endif
    {
      xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, 0, 0, &equBcwCost);
    }

#if GDR_ENABLED
    if (!testBcw && bestCS->cus.size() > 0)
#else
    if (!testBcw)
#endif
    {
      m_pcInterSearch->setAffineModeSelected(bestCS->cus.front()->affine && !(bestCS->cus.front()->firstPU->mergeFlag));
    }

    if (m_pcEncCfg->getUseBcwFast())
    {
      if (equBcwCost > curBestCost * BCW_COST_TH)
      {
        break;
      }
      if (!testBcw && cu.firstPU->interDir != 3 && m_pcEncCfg->getIsLowDelay())
      {
        break;
      }
      if (!testBcw && xIsBcwSkip(cu))
      {
        break;
      }
    }
  }
  if ( m_bestModeUpdated && bestCS->cost != MAX_DOUBLE )
  {
    xCalDebCost( *bestCS, partitioner );
  }
}

bool EncCu::xCheckRDCostInterAmvr(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner,
                                  const EncTestMode &encTestMode, double &bestIntPelCost)
{
  const auto amvrSearchMode = encTestMode.getAmvrSearchMode();
  m_pcInterSearch->setAffineModeSelected(false);
  // Only Half-Pel, int-Pel, 4-Pel and fast 4-Pel allowed
  CHECK(amvrSearchMode < EncTestMode::AmvrSearchMode::FULL_PEL
          || amvrSearchMode > EncTestMode::AmvrSearchMode::HALF_PEL,
        "Unsupported AMVR Mode");
  const bool testAltHpelFilter = amvrSearchMode == EncTestMode::AmvrSearchMode::HALF_PEL;
  // Fast 4-Pel Mode

  m_bestModeUpdated = false;
  tempCS->useDbCost = false;
  bestCS->useDbCost = false;

  EncTestMode encTestModeBase = encTestMode;                                        // copy for clearing non-IMV options
  encTestModeBase.opts        = EncTestModeOpts( encTestModeBase.opts & ETO_IMV );  // clear non-IMV options (is that intended?)

  m_pcInterSearch->resetBufferedUniMotions();

  const int bcwLoopNum = tempCS->slice->isInterB() && tempCS->sps->getUseBcw()
                             && tempCS->area.lwidth() * tempCS->area.lheight() >= BCW_SIZE_CONSTRAINT
                           ? BCW_NUM
                           : 1;

  bool validMode = false;
  double curBestCost = bestCS->cost;
  double equBcwCost = MAX_DOUBLE;

  for( int bcwLoopIdx = 0; bcwLoopIdx < bcwLoopNum; bcwLoopIdx++ )
  {
    if( m_pcEncCfg->getUseBcwFast() )
    {
      const auto blkCache = dynamic_cast<CacheBlkInfoCtrl *>(m_modeCtrl);

      if( blkCache )
      {
        const bool    isBestInter = blkCache->getInter(bestCS->area);
        const uint8_t bestBcwIdx  = blkCache->getBcwIdx(bestCS->area);

        if( isBestInter && g_BcwSearchOrder[bcwLoopIdx] != BCW_DEFAULT && g_BcwSearchOrder[bcwLoopIdx] != bestBcwIdx )
        {
          continue;
        }
      }
    }

    if( !tempCS->slice->getCheckLDC() )
    {
      if( bcwLoopIdx != 0 && bcwLoopIdx != 3 && bcwLoopIdx != 4 )
      {
        continue;
      }
    }

    if (m_pcEncCfg->getUseBcwFast() && tempCS->slice->getCheckLDC() && g_BcwSearchOrder[bcwLoopIdx] != BCW_DEFAULT
        && (m_bestBcwIdx[0] != BCW_NUM && g_BcwSearchOrder[bcwLoopIdx] != m_bestBcwIdx[0])
        && (m_bestBcwIdx[1] != BCW_NUM && g_BcwSearchOrder[bcwLoopIdx] != m_bestBcwIdx[1]))
    {
      continue;
    }

    tempCS->initStructData(encTestMode.qp);

    CodingUnit &cu = tempCS->addCU(tempCS->area, partitioner.chType);

    partitioner.setCUData(cu);
    cu.slice       = tempCS->slice;
    cu.tileIdx     = tempCS->pps->getTileIdx(tempCS->area.lumaPos());
    cu.skip        = false;
    cu.mmvdSkip    = false;
    cu.predMode    = MODE_INTER;
    cu.chromaQpAdj = m_cuChromaQpOffsetIdxPlus1;
    cu.qp          = encTestMode.qp;

    CU::addPUs(cu);

#if GDR_ENABLED
    const bool isEncodeGdrClean =
      tempCS->sps->getGDREnabledFlag() && tempCS->pcv->isEncoder
      && ((tempCS->picHeader->getInGdrInterval() && tempCS->isClean(cu.Y().topRight(), ChannelType::LUMA))
          || (tempCS->picHeader->getNumVerVirtualBoundaries() == 0));
#endif
    if (testAltHpelFilter)
    {
      cu.imv = IMV_HPEL;
    }
    else
    {
      cu.imv = amvrSearchMode == EncTestMode::AmvrSearchMode::FULL_PEL ? IMV_FPEL : IMV_4PEL;
    }

    const bool affineAmvrEnabledFlag = !testAltHpelFilter && cu.slice->getSPS()->getAffineAmvrEnabledFlag();

    cu.bcwIdx = g_BcwSearchOrder[bcwLoopIdx];

    uint8_t    bcwIdx  = cu.bcwIdx;
    const bool testBcw = bcwIdx != BCW_DEFAULT;

    cu.firstPU->interDir = 10;

    m_pcInterSearch->predInterSearch(cu, partitioner);

    if (cu.firstPU->interDir <= 3)
    {
      bcwIdx = CU::getValidBcwIdx(cu);
      CHECK(!testBcw && bcwIdx != BCW_DEFAULT, "Bad BCW index");
    }
    else
    {
      return false;
    }

    if (m_pcEncCfg->getMCTSEncConstraint()
        && ((cu.firstPU->refIdx[L0] < 0 && cu.firstPU->refIdx[L1] < 0)
            || !MCTSHelper::checkMvBufferForMCTSConstraint(*cu.firstPU)))
    {
      // Do not use this mode
      continue;
    }
    if (testBcw && bcwIdx == BCW_DEFAULT)   // Enabled Bcw but the search results is uni.
    {
      continue;
    }

    if (!CU::hasSubCUNonZeroMVd(cu) && !CU::hasSubCUNonZeroAffineMVd(cu))
    {
      if (m_modeCtrl->useModeResult(encTestModeBase, tempCS, partitioner))
      {
        std::swap(tempCS, bestCS);
        // store temp best CI for next CU coding
        m_CurrCtx->best = m_CABACEstimator->getCtx();
      }
      if (affineAmvrEnabledFlag)
      {
        continue;
      }
      else
      {
        return false;
      }
    }

#if GDR_ENABLED
    // 2.0 xCheckRDCostInter: check residual (compare with bestCS)
    if (isEncodeGdrClean)
    {
      bool isClean = true;

      if (cu.affine && cu.firstPU)
      {
        bool L0ok = true, L1ok = true, L3ok = true;

        L0ok = L0ok && cu.firstPU->mvAffiSolid[0][0] && cu.firstPU->mvAffiSolid[0][1] && cu.firstPU->mvAffiSolid[0][2];
        L0ok = L0ok && cu.firstPU->mvAffiValid[0][0] && cu.firstPU->mvAffiValid[0][1] && cu.firstPU->mvAffiValid[0][2];

        L1ok = L1ok && cu.firstPU->mvAffiSolid[1][0] && cu.firstPU->mvAffiSolid[1][1] && cu.firstPU->mvAffiSolid[1][2];
        L1ok = L1ok && cu.firstPU->mvAffiValid[1][0] && cu.firstPU->mvAffiValid[1][1] && cu.firstPU->mvAffiValid[1][2];

        L3ok = L0ok && L1ok;

        if (cu.firstPU->interDir == 1 && !L0ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 2 && !L1ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 3 && !L3ok)
        {
          isClean = false;
        }
      }
      else if (cu.firstPU)
      {
        bool L0ok = cu.firstPU->mvSolid[0] && cu.firstPU->mvValid[0];
        bool L1ok = cu.firstPU->mvSolid[1] && cu.firstPU->mvValid[1];
        bool L3ok = L0ok && L1ok;

        if (cu.firstPU->interDir == 1 && !L0ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 2 && !L1ok)
        {
          isClean = false;
        }
        if (cu.firstPU->interDir == 3 && !L3ok)
        {
          isClean = false;
        }
      }
      else
      {
        isClean = false;
      }

      if (isClean)
      {
        xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, 0, 0, &equBcwCost);
      }
    }
    else
    {
      xEncodeInterResidual(tempCS, bestCS, partitioner, encTestMode, 0, 0, &equBcwCost);
    }
#else
    xEncodeInterResidual(tempCS, bestCS, partitioner, encTestModeBase, 0, 0, &equBcwCost);
#endif

    if (cu.imv == IMV_FPEL && tempCS->cost < bestIntPelCost)
    {
      bestIntPelCost = tempCS->cost;
    }

    if (m_pcEncCfg->getUseBcwFast())
    {
      // Early termination conditions
      if (equBcwCost > curBestCost * BCW_COST_TH)
      {
        break;
      }
      if (!testBcw && cu.firstPU->interDir != 3 && m_pcEncCfg->getIsLowDelay())
      {
        break;
      }
      if (!testBcw && xIsBcwSkip(cu))
      {
        break;
      }
    }

    validMode = true;
  }

  if ( m_bestModeUpdated && bestCS->cost != MAX_DOUBLE )
  {
    xCalDebCost( *bestCS, partitioner );
  }

  return tempCS->slice->getSPS()->getAffineAmvrEnabledFlag() ? validMode : true;
}

void EncCu::xCalDebCost( CodingStructure &cs, Partitioner &partitioner, bool calDist )
{
  if ( cs.cost == MAX_DOUBLE )
  {
    cs.costDbOffset = 0;
  }

  if ( cs.slice->getDeblockingFilterDisable() || ( !m_pcEncCfg->getUseEncDbOpt() && !calDist ) )
  {
    return;
  }

  m_deblockingFilter->setEnc(true);
  const ChromaFormat format = cs.area.chromaFormat;
  CodingUnit*                cu = cs.getCU(partitioner.chType);

  const Position lumaPos      = cu->Y().valid()
                                  ? cu->Y().pos()
                                  : recalcPosition(format, cu->chType, ChannelType::LUMA, cu->block(cu->chType).pos());
  bool topEdgeAvai = lumaPos.y > 0 && ((lumaPos.y % 4) == 0);
  bool leftEdgeAvai = lumaPos.x > 0 && ((lumaPos.x % 4) == 0);
  bool anyEdgeAvai = topEdgeAvai || leftEdgeAvai;
  cs.costDbOffset = 0;

  if ( calDist )
  {
    ComponentID compStr = ( cu->isSepTree() && !isLuma( partitioner.chType ) ) ? COMPONENT_Cb : COMPONENT_Y;
    ComponentID compEnd = ( ( cu->isSepTree() && isLuma( partitioner.chType ) ) || cs.area.chromaFormat == CHROMA_400 ) ? COMPONENT_Y : COMPONENT_Cr;
    Distortion finalDistortion = 0;
    for ( int comp = compStr; comp <= compEnd; comp++ )
    {
      const ComponentID compID = ComponentID( comp );
      CPelBuf org = cs.getOrgBuf( compID );
      CPelBuf reco = cs.getRecoBuf( compID );
      finalDistortion += getDistortionDb(cs, org, reco, compID, cs.area.block(COMPONENT_Y), false);
    }
    //updated distortion
    cs.dist = finalDistortion;
  }

  if ( anyEdgeAvai && m_pcEncCfg->getUseEncDbOpt() )
  {
    ComponentID compStr = ( cu->isSepTree() && !isLuma( partitioner.chType ) ) ? COMPONENT_Cb : COMPONENT_Y;
    ComponentID compEnd = ( ( cu->isSepTree() &&  isLuma( partitioner.chType ) ) || cs.area.chromaFormat == CHROMA_400 ) ? COMPONENT_Y : COMPONENT_Cr;

    const UnitArea currCsArea = clipArea(cs.area, *cs.picture);

    PelStorage&          picDbBuf = m_deblockingFilter->getDbEncPicYuvBuffer();

    //deblock neighbour pixels
    const Size lumaSize = cu->Y().valid()
                            ? cu->Y().size()
                            : recalcSize(format, cu->chType, ChannelType::LUMA, cu->block(cu->chType).size());

    const int verOffset = lumaPos.y > 7 ? 8 : 4;
    const int horOffset = lumaPos.x > 7 ? 8 : 4;
    const UnitArea areaTop(  format, Area( lumaPos.x, lumaPos.y - verOffset, lumaSize.width, verOffset  ) );
    const UnitArea areaLeft( format, Area( lumaPos.x - horOffset, lumaPos.y, horOffset, lumaSize.height ) );
    for ( int compIdx = compStr; compIdx <= compEnd; compIdx++ )
    {
      ComponentID compId = (ComponentID)compIdx;

      //Copy current CU's reco to Deblock Pic Buffer
      const CompArea&  curCompArea = currCsArea.block( compId );
      picDbBuf.getBuf( curCompArea ).copyFrom( cs.getRecoBuf( curCompArea ) );
      if (cs.slice->getLmcsEnabledFlag() && m_pcReshape->getSliceReshaperInfo().getUseSliceReshaper() && isLuma(compId))
      {
        picDbBuf.getBuf( curCompArea ).rspSignal( m_pcReshape->getInvLUT() );
      }

      //left neighbour
      if ( leftEdgeAvai )
      {
        const CompArea&  compArea = areaLeft.block(compId);
        picDbBuf.getBuf( compArea ).copyFrom( cs.picture->getRecoBuf( compArea ) );
        if (cs.slice->getLmcsEnabledFlag() && m_pcReshape->getSliceReshaperInfo().getUseSliceReshaper() && isLuma(compId))
        {
          picDbBuf.getBuf( compArea ).rspSignal( m_pcReshape->getInvLUT() );
        }
      }
      //top neighbour
      if ( topEdgeAvai )
      {
        const CompArea&  compArea = areaTop.block( compId );
        picDbBuf.getBuf( compArea ).copyFrom( cs.picture->getRecoBuf( compArea ) );
        if (cs.slice->getLmcsEnabledFlag() && m_pcReshape->getSliceReshaperInfo().getUseSliceReshaper() && isLuma(compId))
        {
          picDbBuf.getBuf( compArea ).rspSignal( m_pcReshape->getInvLUT() );
        }
      }
    }

    //deblock
    if ( leftEdgeAvai )
    {
      m_deblockingFilter->resetFilterLengths();
      m_deblockingFilter->xDeblockCU( *cu, EDGE_VER );
    }

    if (topEdgeAvai)
    {
      m_deblockingFilter->resetFilterLengths();
      m_deblockingFilter->xDeblockCU( *cu, EDGE_HOR );
    }

    //update current CU SSE
    Distortion distCur = 0;
    for ( int compIdx = compStr; compIdx <= compEnd; compIdx++ )
    {
      ComponentID compId = (ComponentID)compIdx;
      CPelBuf reco = picDbBuf.getBuf( currCsArea.block( compId ) );
      CPelBuf org = cs.getOrgBuf( compId );
      distCur += getDistortionDb(cs, org, reco, compId, currCsArea.block(COMPONENT_Y), true);
    }

    //calculate difference between DB_before_SSE and DB_after_SSE for neighbouring CUs
    Distortion distBeforeDb = 0, distAfterDb = 0;
    for (int compIdx = compStr; compIdx <= compEnd; compIdx++)
    {
      ComponentID compId = (ComponentID)compIdx;
      if ( leftEdgeAvai )
      {
        const CompArea&  compArea = areaLeft.block( compId );
        CPelBuf org = cs.picture->getOrigBuf( compArea );
        CPelBuf reco = cs.picture->getRecoBuf( compArea );
        CPelBuf recoDb = picDbBuf.getBuf( compArea );
        distBeforeDb += getDistortionDb(cs, org, reco, compId, areaLeft.block(COMPONENT_Y), false);
        distAfterDb += getDistortionDb(cs, org, recoDb, compId, areaLeft.block(COMPONENT_Y), true);
      }
      if ( topEdgeAvai )
      {
        const CompArea&  compArea = areaTop.block( compId );
        CPelBuf org = cs.picture->getOrigBuf( compArea );
        CPelBuf reco = cs.picture->getRecoBuf( compArea );
        CPelBuf recoDb = picDbBuf.getBuf( compArea );
        distBeforeDb += getDistortionDb(cs, org, reco, compId, areaTop.block(COMPONENT_Y), false);
        distAfterDb += getDistortionDb(cs, org, recoDb, compId, areaTop.block(COMPONENT_Y), true);
      }
    }

    //updated cost
    int64_t distTmp = distCur - cs.dist + distAfterDb - distBeforeDb;
    const int sign    = sgn2(distTmp);
    distTmp = distTmp < 0 ? -distTmp : distTmp;
    cs.costDbOffset = sign * m_pcRdCost->calcRdCost( 0, distTmp );
  }

  m_deblockingFilter->setEnc( false );
}

Distortion EncCu::getDistortionDb( CodingStructure &cs, CPelBuf org, CPelBuf reco, ComponentID compID, const CompArea& compArea, bool afterDb )
{
  Distortion dist = 0;
#if WCG_EXT
  m_pcRdCost->setChromaFormat(cs.sps->getChromaFormatIdc());
  CPelBuf orgLuma = cs.picture->getOrigBuf(compArea);
  if (m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled() || (
    m_pcEncCfg->getLmcs() && (cs.slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())))
  {
    if ( compID == COMPONENT_Y && !afterDb && !m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled())
    {
      CompArea    tmpArea( COMPONENT_Y, cs.area.chromaFormat, Position( 0, 0 ), compArea.size() );
      PelBuf tmpRecLuma = m_tmpStorageLCU->getBuf( tmpArea );
      tmpRecLuma.copyFrom( reco );
      tmpRecLuma.rspSignal( m_pcReshape->getInvLUT() );
      dist += m_pcRdCost->getDistPart( org, tmpRecLuma, cs.sps->getBitDepth( toChannelType( compID ) ), compID, DF_SSE_WTD, &orgLuma );
    }
    else
    {
      dist += m_pcRdCost->getDistPart( org, reco, cs.sps->getBitDepth( toChannelType( compID ) ), compID, DF_SSE_WTD, &orgLuma );
    }
  }
  else if (m_pcEncCfg->getLmcs() && cs.slice->getLmcsEnabledFlag() && cs.slice->isIntra()) //intra slice
  {
    if ( compID == COMPONENT_Y && afterDb )
    {
      CompArea    tmpArea( COMPONENT_Y, cs.area.chromaFormat, Position( 0, 0 ), compArea.size() );
      PelBuf tmpRecLuma = m_tmpStorageLCU->getBuf( tmpArea );
      tmpRecLuma.copyFrom( reco );
      tmpRecLuma.rspSignal( m_pcReshape->getFwdLUT() );
      dist += m_pcRdCost->getDistPart( org, tmpRecLuma, cs.sps->getBitDepth( toChannelType( compID ) ), compID, DF_SSE );
    }
    else
    {
      if ((isChroma(compID) && m_pcEncCfg->getReshapeIntraCMD()))
      {
        dist +=
          m_pcRdCost->getDistPart(org, reco, cs.sps->getBitDepth(toChannelType(compID)), compID, DF_SSE_WTD, &orgLuma);
      }
      else
      {
        dist += m_pcRdCost->getDistPart( org, reco, cs.sps->getBitDepth(toChannelType( compID ) ), compID, DF_SSE );
      }
    }
  }
  else
#endif
  {
    dist = m_pcRdCost->getDistPart( org, reco, cs.sps->getBitDepth( toChannelType( compID ) ), compID, DF_SSE );
  }
  return dist;
}

void EncCu::xEncodeInterResidual(CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner,
                                 const EncTestMode &encTestMode, int residualPass, bool *bestHasNonResi,
                                 double *equBcwCost)
{
  CodingUnit*            cu        = tempCS->getCU( partitioner.chType );
  double   bestCostInternal        = MAX_DOUBLE;
  double           bestCost        = bestCS->cost;
  double           bestCostBegin   = bestCS->cost;
  CodingUnit*      prevBestCU      = bestCS->getCU( partitioner.chType );
  uint8_t          prevBestSbt     = ( prevBestCU == nullptr ) ? 0 : prevBestCU->sbtInfo;
  bool              swapped        = false; // avoid unwanted data copy
  bool             reloadCU        = false;

  const PredictionUnit& pu = *cu->firstPU;

  // Check whether MV and MVD are in valid range
  for (int refList = 0; refList < NUM_REF_PIC_LIST_01; refList++)
  {
    if (pu.refIdx[refList] >= 0)
    {
      if (!cu->affine)
      {
        if (!pu.mv[refList].isInRange())
        {
          return;
        }

        Mv signaledMvd = pu.mvd[refList];
        signaledMvd.changeTransPrecInternal2Amvr(cu->imv);

        if (!signaledMvd.isInRangeDelta())
        {
          return;
        }
      }
      else
      {
        for (int ctrlP = cu->getNumAffineMvs() - 1; ctrlP >= 0; ctrlP--)
        {
          if (!pu.mvAffi[refList][ctrlP].isInRange())
          {
            return;
          }

          Mv signaledMvd = pu.mvdAffi[refList][ctrlP];
          signaledMvd.changeAffinePrecInternal2Amvr(cu->imv);

          if (!signaledMvd.isInRangeDelta())
          {
            return;
          }
        }
      }
    }
  }

  const bool mtsAllowed = tempCS->sps->getExplicitMtsInterEnabled() && CU::isInter(*cu)
                          && partitioner.currArea().lwidth() <= MTS_INTER_MAX_CU_SIZE
                          && partitioner.currArea().lheight() <= MTS_INTER_MAX_CU_SIZE;
  uint8_t sbtAllowed = cu->checkAllowedSbt();
  //SBT resolution-dependent fast algorithm: not try size-64 SBT in RDO for low-resolution sequences (now resolution below HD)
  if( tempCS->pps->getPicWidthInLumaSamples() < (uint32_t)m_pcEncCfg->getSBTFast64WidthTh() )
  {
    sbtAllowed = ((cu->lwidth() > 32 || cu->lheight() > 32)) ? 0 : sbtAllowed;
  }
  uint8_t numRDOTried = 0;
  Distortion sbtOffDist = 0;
  bool    sbtOffRootCbf = 0;
  double  sbtOffCost      = MAX_DOUBLE;
  double  currBestCost = MAX_DOUBLE;
  bool    doPreAnalyzeResi = ( sbtAllowed || mtsAllowed ) && residualPass == 0;

  m_pcInterSearch->initTuAnalyzer();
  if( doPreAnalyzeResi )
  {
    m_pcInterSearch->calcMinDistSbt( *tempCS, *cu, sbtAllowed );
  }

  auto    slsSbt = dynamic_cast<SaveLoadEncInfoSbt*>( m_modeCtrl );
  int     slShift = 4 + std::min( (int)gp_sizeIdxInfo->idxFrom( cu->lwidth() ) + (int)gp_sizeIdxInfo->idxFrom( cu->lheight() ), 9 );
  Distortion curPuSse = m_pcInterSearch->getEstDistSbt( NUMBER_SBT_MODE );
  uint8_t currBestSbt = 0;
  auto       currBestTrs = MtsType::NONE;
  uint8_t histBestSbt = MAX_UCHAR;
  auto       histBestTrs = MtsType::NONE;
  m_pcInterSearch->setHistBestTrs(MAX_UCHAR, MtsType::NONE);
  if( doPreAnalyzeResi )
  {
    if( m_pcInterSearch->getSkipSbtAll() && !mtsAllowed ) //emt is off
    {
      histBestSbt = 0; //try DCT2
      m_pcInterSearch->setHistBestTrs( histBestSbt, histBestTrs );
    }
    else
    {
      assert( curPuSse != std::numeric_limits<uint64_t>::max() );
      SaveLoadEncInfoSbt::BestSbt compositeSbtTrs = slsSbt->findBestSbt(cu->cs->area, (uint32_t) (curPuSse >> slShift));

      histBestSbt = compositeSbtTrs.sbt;
      histBestTrs = compositeSbtTrs.trs;
      if( m_pcInterSearch->getSkipSbtAll() && CU::isSbtMode( histBestSbt ) ) //special case, skip SBT when loading SBT
      {
        histBestSbt = 0; //try DCT2
      }
      m_pcInterSearch->setHistBestTrs( histBestSbt, histBestTrs );
    }
  }

  {
    if( reloadCU )
    {
      if( bestCost == bestCS->cost ) //The first EMT pass didn't become the bestCS, so we clear the TUs generated
      {
        tempCS->clearTUs();
      }
      else if( false == swapped )
      {
        tempCS->initStructData( encTestMode.qp );
        tempCS->copyStructure( *bestCS, partitioner.chType );
        tempCS->getPredBuf().copyFrom( bestCS->getPredBuf() );
        bestCost = bestCS->cost;
        cu       = tempCS->getCU( partitioner.chType );
        swapped = true;
      }
      else
      {
        tempCS->clearTUs();
        bestCost = bestCS->cost;
        cu       = tempCS->getCU( partitioner.chType );
      }

      //we need to restart the distortion for the new tempCS, the bit count and the cost
      tempCS->dist     = 0;
      tempCS->fracBits = 0;
      tempCS->cost     = MAX_DOUBLE;
      tempCS->costDbOffset = 0;
    }

    reloadCU    = true; // enable cu reloading
    cu->skip    = false;
    cu->sbtInfo = 0;

    const bool skipResidual = residualPass == 1;
    if( skipResidual || histBestSbt == MAX_UCHAR || !CU::isSbtMode( histBestSbt ) )
    {
      m_pcInterSearch->encodeResAndCalcRdInterCU(*tempCS, partitioner, skipResidual);
      if (tempCS->slice->getSPS()->getUseColorTrans())
      {
        bestCS->tmpColorSpaceCost       = tempCS->tmpColorSpaceCost;
        bestCS->firstColorSpaceSelected = tempCS->firstColorSpaceSelected;
      }
      numRDOTried += mtsAllowed ? 2 : 1;
      xEncodeDontSplit(*tempCS, partitioner);

      xCheckDQP(*tempCS, partitioner);
      xCheckChromaQPOffset(*tempCS, partitioner);

      if (nullptr != bestHasNonResi && (bestCostInternal > tempCS->cost))
      {
        bestCostInternal = tempCS->cost;
        if (!(tempCS->getPU(partitioner.chType)->ciipFlag))
          *bestHasNonResi = !cu->rootCbf;
      }

      if (cu->rootCbf == false)
      {
        if (tempCS->getPU(partitioner.chType)->ciipFlag)
        {
          tempCS->cost         = MAX_DOUBLE;
          tempCS->costDbOffset = 0;
          return;
        }
      }
      currBestCost  = tempCS->cost;
      sbtOffCost    = tempCS->cost;
      sbtOffDist    = tempCS->dist;
      sbtOffRootCbf = cu->rootCbf;
      currBestSbt   = CU::getSbtInfo(cu->firstTU->mtsIdx[COMPONENT_Y] > MtsType::SKIP ? SBT_OFF_MTS : SBT_OFF_DCT, 0);
      currBestTrs   = cu->firstTU->mtsIdx[COMPONENT_Y];

#if WCG_EXT
      DTRACE_MODE_COST(*tempCS, m_pcRdCost->getLambda(true));
#else
      DTRACE_MODE_COST(*tempCS, m_pcRdCost->getLambda());
#endif
      xCheckBestMode(tempCS, bestCS, partitioner, encTestMode);
    }

    uint8_t numSbtRdo = CU::numSbtModeRdo( sbtAllowed );
    //early termination if all SBT modes are not allowed
    //normative
    if( !sbtAllowed || skipResidual )
    {
      numSbtRdo = 0;
    }
    //fast algorithm
    if( ( histBestSbt != MAX_UCHAR && !CU::isSbtMode( histBestSbt ) ) || m_pcInterSearch->getSkipSbtAll() )
    {
      numSbtRdo = 0;
    }
    if( bestCost != MAX_DOUBLE && sbtOffCost != MAX_DOUBLE )
    {
      double th = 1.07;
      if( !( prevBestSbt == 0 || m_sbtCostSave[0] == MAX_DOUBLE ) )
      {
        assert( m_sbtCostSave[1] <= m_sbtCostSave[0] );
        th *= ( m_sbtCostSave[0] / m_sbtCostSave[1] );
      }
      if( sbtOffCost > bestCost * th )
      {
        numSbtRdo = 0;
      }
    }
    if( !sbtOffRootCbf && sbtOffCost != MAX_DOUBLE )
    {
      double th = Clip3( 0.05, 0.55, ( 27 - cu->qp ) * 0.02 + 0.35 );
      if( sbtOffCost < m_pcRdCost->calcRdCost( ( cu->lwidth() * cu->lheight() ) << SCALE_BITS, 0 ) * th )
      {
        numSbtRdo = 0;
      }
    }

    if( histBestSbt != MAX_UCHAR && numSbtRdo != 0 )
    {
      numSbtRdo = 1;
      m_pcInterSearch->initSbtRdoOrder( CU::getSbtMode( CU::getSbtIdx( histBestSbt ), CU::getSbtPos( histBestSbt ) ) );
    }

    for( int sbtModeIdx = 0; sbtModeIdx < numSbtRdo; sbtModeIdx++ )
    {
      uint8_t sbtMode = m_pcInterSearch->getSbtRdoOrder( sbtModeIdx );
      uint8_t sbtIdx = CU::getSbtIdxFromSbtMode( sbtMode );
      uint8_t sbtPos = CU::getSbtPosFromSbtMode( sbtMode );

      //fast algorithm (early skip, save & load)
      if( histBestSbt == MAX_UCHAR )
      {
        uint8_t skipCode = m_pcInterSearch->skipSbtByRDCost( cu->lwidth(), cu->lheight(), cu->mtDepth, sbtIdx, sbtPos, bestCS->cost, sbtOffDist, sbtOffCost, sbtOffRootCbf );
        if( skipCode != MAX_UCHAR )
        {
          continue;
        }

        if( sbtModeIdx > 0 )
        {
          uint8_t prevSbtMode = m_pcInterSearch->getSbtRdoOrder( sbtModeIdx - 1 );
          //make sure the prevSbtMode is the same size as the current SBT mode (otherwise the estimated dist may not be comparable)
          if( CU::isSameSbtSize( prevSbtMode, sbtMode ) )
          {
            Distortion currEstDist = m_pcInterSearch->getEstDistSbt( sbtMode );
            Distortion prevEstDist = m_pcInterSearch->getEstDistSbt( prevSbtMode );
            if( currEstDist > prevEstDist * 1.15 )
            {
              continue;
            }
          }
        }
      }

      //init tempCS and TU
      if( bestCost == bestCS->cost ) //The first EMT pass didn't become the bestCS, so we clear the TUs generated
      {
        tempCS->clearTUs();
      }
      else if( false == swapped )
      {
        tempCS->initStructData( encTestMode.qp );
        tempCS->copyStructure( *bestCS, partitioner.chType );
        tempCS->getPredBuf().copyFrom( bestCS->getPredBuf() );
        bestCost = bestCS->cost;
        cu = tempCS->getCU( partitioner.chType );
        swapped = true;
      }
      else
      {
        tempCS->clearTUs();
        bestCost = bestCS->cost;
        cu = tempCS->getCU( partitioner.chType );
      }

      //we need to restart the distortion for the new tempCS, the bit count and the cost
      tempCS->dist = 0;
      tempCS->fracBits = 0;
      tempCS->cost = MAX_DOUBLE;
      cu->skip = false;

      //set SBT info
      cu->setSbtIdx( sbtIdx );
      cu->setSbtPos( sbtPos );

      //try residual coding
      m_pcInterSearch->encodeResAndCalcRdInterCU( *tempCS, partitioner, skipResidual );
      if (tempCS->slice->getSPS()->getUseColorTrans())
      {
        bestCS->tmpColorSpaceCost = tempCS->tmpColorSpaceCost;
        bestCS->firstColorSpaceSelected = tempCS->firstColorSpaceSelected;
      }
      numRDOTried++;

      xEncodeDontSplit( *tempCS, partitioner );

      xCheckDQP( *tempCS, partitioner );
      xCheckChromaQPOffset( *tempCS, partitioner );

      if (nullptr != bestHasNonResi && (bestCostInternal > tempCS->cost))
      {
        bestCostInternal = tempCS->cost;
        if( !( tempCS->getPU( partitioner.chType )->ciipFlag ) )
          *bestHasNonResi = !cu->rootCbf;
      }

      if( tempCS->cost < currBestCost )
      {
        currBestSbt = cu->sbtInfo;
        currBestTrs = tempCS->tus[cu->sbtInfo ? cu->getSbtPos() : 0]->mtsIdx[COMPONENT_Y];
        assert(currBestTrs == MtsType::DCT2_DCT2 || currBestTrs == MtsType::SKIP);
        currBestCost = tempCS->cost;
      }

#if WCG_EXT
      DTRACE_MODE_COST( *tempCS, m_pcRdCost->getLambda( true ) );
#else
      DTRACE_MODE_COST( *tempCS, m_pcRdCost->getLambda() );
#endif
      xCheckBestMode( tempCS, bestCS, partitioner, encTestMode );
    }

    if( bestCostBegin != bestCS->cost )
    {
      m_sbtCostSave[0] = sbtOffCost;
      m_sbtCostSave[1] = currBestCost;
    }
  } //end emt loop

  if( histBestSbt == MAX_UCHAR && doPreAnalyzeResi && numRDOTried > 1 )
  {
    slsSbt->saveBestSbt( cu->cs->area, (uint32_t)( curPuSse >> slShift ), currBestSbt, currBestTrs );
  }
  tempCS->cost = currBestCost;
  if( ETM_INTER_ME == encTestMode.type )
  {
    if (equBcwCost != nullptr)
    {
      if (tempCS->cost < (*equBcwCost) && cu->bcwIdx == BCW_DEFAULT)
      {
        ( *equBcwCost ) = tempCS->cost;
      }
    }
    else
    {
      CHECK(equBcwCost == nullptr, "equBcwCost == nullptr");
    }
    if (tempCS->slice->getCheckLDC() && !cu->imv && cu->bcwIdx != BCW_DEFAULT && tempCS->cost < m_bestBcwCost[1])
    {
      if( tempCS->cost < m_bestBcwCost[0] )
      {
        m_bestBcwCost[1] = m_bestBcwCost[0];
        m_bestBcwCost[0] = tempCS->cost;
        m_bestBcwIdx[1] = m_bestBcwIdx[0];
        m_bestBcwIdx[0]  = cu->bcwIdx;
      }
      else
      {
        m_bestBcwCost[1] = tempCS->cost;
        m_bestBcwIdx[1]  = cu->bcwIdx;
      }
    }
  }
}


void EncCu::xEncodeDontSplit( CodingStructure &cs, Partitioner &partitioner )
{
  m_CABACEstimator->resetBits();

  m_CABACEstimator->split_cu_mode( CU_DONT_SPLIT, cs, partitioner );
  if( partitioner.treeType == TREE_C )
  {
    CHECK( m_CABACEstimator->getEstFracBits() != 0, "must be 0 bit" );
  }

  cs.fracBits += m_CABACEstimator->getEstFracBits(); // split bits
  cs.cost      = m_pcRdCost->calcRdCost( cs.fracBits, cs.dist );
}

#if REUSE_CU_RESULTS
void EncCu::xReuseCachedResult( CodingStructure *&tempCS, CodingStructure *&bestCS, Partitioner &partitioner )
{
  m_pcRdCost->setChromaFormat(tempCS->sps->getChromaFormatIdc());
  BestEncInfoCache* bestEncCache = dynamic_cast<BestEncInfoCache*>( m_modeCtrl );
  CHECK( !bestEncCache, "If this mode is chosen, mode controller has to implement the mode caching capabilities" );
  EncTestMode cachedMode;

  if( bestEncCache->setCsFrom( *tempCS, cachedMode, partitioner ) )
  {
    CodingUnit& cu = *tempCS->cus.front();
    partitioner.setCUData( cu );

    if (CU::isIntra(cu) || CU::isPLT(cu))
    {
      xReconIntraQT( cu );
    }
    else
    {
      xDeriveCuMvs(cu);
      xReconInter( cu );
    }

    Distortion finalDistortion = 0;
    tempCS->useDbCost = m_pcEncCfg->getUseEncDbOpt();
    if (!tempCS->slice->getDeblockingFilterDisable() && m_pcEncCfg->getUseEncDbOpt())
    {
      xCalDebCost( *tempCS, partitioner, true );
      finalDistortion = tempCS->dist;
    }
    else
    {
      const SPS &sps                = *tempCS->sps;
      const int  numValidComponents = getNumberValidComponents(tempCS->area.chromaFormat);

      for (int comp = 0; comp < numValidComponents; comp++)
      {
        const ComponentID compID = ComponentID(comp);

        if (partitioner.isSepTree(*tempCS) && toChannelType(compID) != partitioner.chType)
        {
          continue;
        }

        CPelBuf reco = tempCS->getRecoBuf(compID);
        CPelBuf org  = tempCS->getOrgBuf(compID);

#if WCG_EXT
        if (m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled()
            || (m_pcEncCfg->getLmcs() && (tempCS->slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())))
        {
          const CPelBuf orgLuma = tempCS->getOrgBuf(tempCS->area.blocks[COMPONENT_Y]);
          if (compID == COMPONENT_Y && !(m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled()))
          {
            const CompArea &area = cu.blocks[COMPONENT_Y];
            CompArea        tmpArea(COMPONENT_Y, area.chromaFormat, Position(0, 0), area.size());
            PelBuf          tmpRecLuma = m_tmpStorageLCU->getBuf(tmpArea);
            tmpRecLuma.copyFrom(reco);
            tmpRecLuma.rspSignal(m_pcReshape->getInvLUT());
            finalDistortion += m_pcRdCost->getDistPart(org, tmpRecLuma, sps.getBitDepth(toChannelType(compID)), compID,
                                                       DF_SSE_WTD, &orgLuma);
          }
          else
          {
            finalDistortion +=
              m_pcRdCost->getDistPart(org, reco, sps.getBitDepth(toChannelType(compID)), compID, DF_SSE_WTD, &orgLuma);
          }
        }
        else
#endif
        {
          finalDistortion += m_pcRdCost->getDistPart(org, reco, sps.getBitDepth(toChannelType(compID)), compID, DF_SSE);
        }
      }
    }

    m_CABACEstimator->getCtx() = m_CurrCtx->start;
    m_CABACEstimator->resetBits();

    CUCtx cuCtx;
    cuCtx.isDQPCoded = true;
    cuCtx.isChromaQpAdjCoded = true;
    m_CABACEstimator->coding_unit( cu, partitioner, cuCtx );


    tempCS->dist     = finalDistortion;
    tempCS->fracBits = m_CABACEstimator->getEstFracBits();
    tempCS->cost     = m_pcRdCost->calcRdCost( tempCS->fracBits, tempCS->dist );

    xEncodeDontSplit( *tempCS,         partitioner );
    xCheckDQP       ( *tempCS,         partitioner );
    xCheckChromaQPOffset( *tempCS,     partitioner );
    xCheckBestMode  (  tempCS, bestCS, partitioner, cachedMode );
  }
  else
  {
    THROW( "Should never happen!" );
  }
}
#endif


//! \}
