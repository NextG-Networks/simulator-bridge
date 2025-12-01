/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 Northeastern University
 * Copyright (c) 2022 Sapienza, University of Rome
 * Copyright (c) 2022 University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Andrea Lacava <thecave003@gmail.com>
 *		   Tommaso Zugno <tommasozugno@gmail.com>
 *		   Michele Polese <michele.polese@gmail.com>
 */

#include <ns3/mmwave-indication-message-helper.h>

namespace ns3 {

MmWaveIndicationMessageHelper::MmWaveIndicationMessageHelper (IndicationMessageType type,
                                                              bool isOffline, bool reducedPmValues)
    : IndicationMessageHelper (type, isOffline, reducedPmValues)
{
}

void
MmWaveIndicationMessageHelper::AddCuUpUePmItem (std::string ueImsiComplete, long txBytes,
                                                long txDlPackets, double pdcpThroughput,
                                                double pdcpLatency, double dlBler)
{
  Ptr<MeasurementItemList> ueVal = Create<MeasurementItemList> (ueImsiComplete);

  if (!m_reducedPmValues)
    {
      // Keep only the most critical measurements to reduce message size and prevent E2 termination crashes
      // UE-specific Downlink IP delay from mmWave gNB
      ueVal->AddItem<double> ("DRB.PdcpSduDelayDl.UEID", pdcpLatency);

      // UE-specific Downlink Block Error Rate (BLER) - percentage (0.0 to 100.0)
      // Always include BLER (even if 0.0) for consistency
      ueVal->AddItem<double> ("DRB.BlerDl.UEID", dlBler * 100.0);  // Convert to percentage
      
      // Optional: Add throughput if needed (commented out to reduce message size)
      // ueVal->AddItem<double> ("DRB.PdcpSduBitRateDl.UEID", pdcpThroughput);
    }

  m_msgValues.m_ueIndications.insert (ueVal);
}

void
MmWaveIndicationMessageHelper::AddCuUpCellPmItem (double cellAverageLatency)
{
  if (!m_reducedPmValues)
    {
      Ptr<MeasurementItemList> cellVal = Create<MeasurementItemList> ();
      cellVal->AddItem<double> ("DRB.PdcpSduDelayDl", cellAverageLatency);
      m_msgValues.m_cellMeasurementItems = cellVal;
    }
}

void
MmWaveIndicationMessageHelper::FillCuUpValues (std::string plmId, long pdcpBytesUl, long pdcpBytesDl)
{
  m_cuUpValues->m_pDCPBytesUL = pdcpBytesUl;
  m_cuUpValues->m_pDCPBytesDL = pdcpBytesDl;
  FillBaseCuUpValues (plmId);
}

void
MmWaveIndicationMessageHelper::FillCuCpValues (uint16_t numActiveUes)
{
  FillBaseCuCpValues (numActiveUes);
}

void
MmWaveIndicationMessageHelper::FillDuValues (std::string cellObjectId)
{
  m_msgValues.m_cellObjectId = cellObjectId;
  m_msgValues.m_pmContainerValues = m_duValues;
}

void
MmWaveIndicationMessageHelper::AddDuUePmItem (
    std::string ueImsiComplete, long macPduUe, long macPduInitialUe, long macQpsk, long mac16Qam,
    long mac64Qam, long macRetx, long macVolume, long macPrb, long macMac04, long macMac59,
    long macMac1014, long macMac1519, long macMac2024, long macMac2529, long macSinrBin1,
    long macSinrBin2, long macSinrBin3, long macSinrBin4, long macSinrBin5, long macSinrBin6,
    long macSinrBin7, long rlcBufferOccup, double drbThrDlUeid)
{

  Ptr<MeasurementItemList> ueVal = Create<MeasurementItemList> (ueImsiComplete);
  if (!m_reducedPmValues)
    {
      // Keep only essential measurements to reduce message size
      // TB counts and modulation schemes (already in CSV)
      ueVal->AddItem<long> ("TB.TotNbrDlInitial.Qpsk.UEID", macQpsk);
      ueVal->AddItem<long> ("TB.TotNbrDlInitial.16Qam.UEID", mac16Qam);
      ueVal->AddItem<long> ("TB.TotNbrDlInitial.64Qam.UEID", mac64Qam);
      ueVal->AddItem<long> ("RRU.PrbUsedDl.UEID", (long) std::ceil (macPrb));
      
      // Removed to reduce message size:
      // - MCS distribution bins (6 bins) - too detailed
      // - SINR bins (7 bins) - too detailed
      // - Buffer size, error counts, volume - less critical
    }

  // Throughput is essential, keep it
  ueVal->AddItem<double> ("DRB.UEThpDl.UEID", drbThrDlUeid);

  m_msgValues.m_ueIndications.insert (ueVal);
}

void
MmWaveIndicationMessageHelper::AddDuCellPmItem (
    long macPduCellSpecific, long macPduInitialCellSpecific, long macQpskCellSpecific,
    long mac16QamCellSpecific, long mac64QamCellSpecific, double prbUtilizationDl,
    long macRetxCellSpecific, long macVolumeCellSpecific, long macMac04CellSpecific,
    long macMac59CellSpecific, long macMac1014CellSpecific, long macMac1519CellSpecific,
    long macMac2024CellSpecific, long macMac2529CellSpecific, long macSinrBin1CellSpecific,
    long macSinrBin2CellSpecific, long macSinrBin3CellSpecific, long macSinrBin4CellSpecific,
    long macSinrBin5CellSpecific, long macSinrBin6CellSpecific, long macSinrBin7CellSpecific,
    long rlcBufferOccupCellSpecific, long activeUeDl)
{
  Ptr<MeasurementItemList> cellVal = Create<MeasurementItemList> ();

  if (!m_reducedPmValues)
    {
      // Keep only essential cell-level measurements to reduce message size
      // Modulation scheme counts (already in CSV)
      cellVal->AddItem<long> ("TB.TotNbrDlInitial.Qpsk", macQpskCellSpecific);
      cellVal->AddItem<long> ("TB.TotNbrDlInitial.16Qam", mac16QamCellSpecific);
      cellVal->AddItem<long> ("TB.TotNbrDlInitial.64Qam", mac64QamCellSpecific);
      cellVal->AddItem<long> ("RRU.PrbUsedDl", (long) std::ceil (prbUtilizationDl));
      
      // Removed to reduce message size:
      // - MCS distribution bins (6 bins) - too detailed
      // - SINR bins (7 bins) - too detailed
      // - Error counts, volume, buffer - less critical
    }

  // Mean active UEs is essential, keep it
  cellVal->AddItem<long> ("DRB.MeanActiveUeDl",activeUeDl);

  m_msgValues.m_cellMeasurementItems = cellVal;
}

void
MmWaveIndicationMessageHelper::AddDuCellResRepPmItem (Ptr<CellResourceReport> cellResRep)
{
  m_duValues->m_cellResourceReportItems.insert (cellResRep);
}

void
MmWaveIndicationMessageHelper::AddCuCpUePmItem (std::string ueImsiComplete, long numDrb,
                                                long drbRelAct,
                                                Ptr<L3RrcMeasurements> l3RrcMeasurementServing,
                                                Ptr<L3RrcMeasurements> l3RrcMeasurementNeigh)
{

  Ptr<MeasurementItemList> ueVal = Create<MeasurementItemList> (ueImsiComplete);
  if (!m_reducedPmValues)
    {
      ueVal->AddItem<long> ("DRB.EstabSucc.5QI.UEID", numDrb);
      ueVal->AddItem<long> ("DRB.RelActNbr.5QI.UEID", drbRelAct); // not modeled in the simulator
    }

  ueVal->AddItem<Ptr<L3RrcMeasurements>> ("HO.SrcCellQual.RS-SINR.UEID", l3RrcMeasurementServing);
  ueVal->AddItem<Ptr<L3RrcMeasurements>> ("HO.TrgtCellQual.RS-SINR.UEID", l3RrcMeasurementNeigh);

  m_msgValues.m_ueIndications.insert (ueVal);
}

MmWaveIndicationMessageHelper::~MmWaveIndicationMessageHelper ()
{
}

} // namespace ns3