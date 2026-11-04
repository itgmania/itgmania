#include "CombinedLifeMeterShared.h"

#include "LifeMeter.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "RageUtil.h"

CombinedLifeMeterShared::CombinedLifeMeterShared(
    const PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats,
    LifeType lifeType)
    : life_meter_(LifeMeter::MakeLifeMeter(lifeType)) {
  ASSERT(life_meter_ != nullptr);
  life_meter_->Load(pPlayerState, pPlayerStageStats);
  life_meter_->SetName("CoopLifeMeterInternal");
  this->AddChild(life_meter_);
}

CombinedLifeMeterShared::~CombinedLifeMeterShared() {
  this->RemoveChild(life_meter_);
  RageUtil::SafeDelete(life_meter_);
}

void CombinedLifeMeterShared::OnLoadSong() {
  if (life_meter_) {
    life_meter_->OnLoadSong();
  }
}

void CombinedLifeMeterShared::ChangeLife(
    PlayerNumber /*pn*/, TapNoteScore tns) {
  if (life_meter_) {
    life_meter_->ChangeLife(tns);
  }
}

void CombinedLifeMeterShared::ChangeLife(
    PlayerNumber /*pn*/, HoldNoteScore hns, TapNoteScore tns) {
  if (life_meter_) {
    life_meter_->ChangeLife(hns, tns);
  }
}

void CombinedLifeMeterShared::ChangeLife(PlayerNumber /*pn*/, float delta) {
  if (life_meter_) {
    life_meter_->ChangeLife(delta);
  }
}

void CombinedLifeMeterShared::SetLife(PlayerNumber /*pn*/, float value) {
  if (life_meter_) {
    life_meter_->SetLife(value);
  }
}

void CombinedLifeMeterShared::HandleTapScoreNone(PlayerNumber /*pn*/) {
  if (life_meter_) {
    life_meter_->HandleTapScoreNone();
  }
}

bool CombinedLifeMeterShared::IsInDanger() const {
  return life_meter_ ? life_meter_->IsInDanger() : false;
}

bool CombinedLifeMeterShared::IsHot() const {
  return life_meter_ ? life_meter_->IsHot() : false;
}

bool CombinedLifeMeterShared::IsFailing() const {
  return life_meter_ ? life_meter_->IsFailing() : false;
}

LifeMeter* CombinedLifeMeterShared::GetInnerLifeMeter() const {
  return life_meter_;
}
