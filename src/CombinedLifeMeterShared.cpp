#include "CombinedLifeMeterShared.h"

#include "LifeMeter.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "RageUtil.h"

CombinedLifeMeterShared::CombinedLifeMeterShared(
    const PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats,
    LifeType lifeType)
    : m_pLifeMeter(LifeMeter::MakeLifeMeter(lifeType)) {
  ASSERT(m_pLifeMeter != nullptr);
  m_pLifeMeter->Load(pPlayerState, pPlayerStageStats);
  m_pLifeMeter->SetName("CoopLifeMeterInternal");
  this->AddChild(m_pLifeMeter);
}

CombinedLifeMeterShared::~CombinedLifeMeterShared() {
  this->RemoveChild(m_pLifeMeter);
  RageUtil::SafeDelete(m_pLifeMeter);
}

void CombinedLifeMeterShared::OnLoadSong() {
  if (m_pLifeMeter) {
    m_pLifeMeter->OnLoadSong();
  }
}

void CombinedLifeMeterShared::ChangeLife(PlayerNumber /*pn*/, TapNoteScore tns) {
  if (m_pLifeMeter) {
    m_pLifeMeter->ChangeLife(tns);
  }
}

void CombinedLifeMeterShared::ChangeLife(
    PlayerNumber /*pn*/, HoldNoteScore hns, TapNoteScore tns) {
  if (m_pLifeMeter) {
    m_pLifeMeter->ChangeLife(hns, tns);
  }
}

void CombinedLifeMeterShared::ChangeLife(PlayerNumber /*pn*/, float delta) {
  if (m_pLifeMeter) {
    m_pLifeMeter->ChangeLife(delta);
  }
}

void CombinedLifeMeterShared::SetLife(PlayerNumber /*pn*/, float value) {
  if (m_pLifeMeter) {
    m_pLifeMeter->SetLife(value);
  }
}

void CombinedLifeMeterShared::HandleTapScoreNone(PlayerNumber /*pn*/) {
  if (m_pLifeMeter) {
    m_pLifeMeter->HandleTapScoreNone();
  }
}

bool CombinedLifeMeterShared::IsInDanger() const {
  return m_pLifeMeter ? m_pLifeMeter->IsInDanger() : false;
}

bool CombinedLifeMeterShared::IsHot() const {
  return m_pLifeMeter ? m_pLifeMeter->IsHot() : false;
}

bool CombinedLifeMeterShared::IsFailing() const {
  return m_pLifeMeter ? m_pLifeMeter->IsFailing() : false;
}

float CombinedLifeMeterShared::GetLife() const {
  return m_pLifeMeter ? m_pLifeMeter->GetLife() : 0.0f;
}

LifeMeter* CombinedLifeMeterShared::GetInnerLifeMeter() const {
  return m_pLifeMeter;
}
