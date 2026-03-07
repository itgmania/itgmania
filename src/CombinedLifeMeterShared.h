#ifndef COMBINED_LIFE_METER_SHARED_H
#define COMBINED_LIFE_METER_SHARED_H

#include "CombinedLifeMeter.h"
#include "LifeMeter.h"

class PlayerState;
class PlayerStageStats;

/** @brief Cooperative combined life meter that shares one LifeMeter pool. */
class CombinedLifeMeterShared : public CombinedLifeMeter {
 public:
  CombinedLifeMeterShared(
      const PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats,
      LifeType lifeType);
  ~CombinedLifeMeterShared() override;

  void OnLoadSong() override;
  void ChangeLife(PlayerNumber pn, TapNoteScore tns) override;
  void ChangeLife(PlayerNumber pn, HoldNoteScore hns, TapNoteScore tns) override;
  void ChangeLife(PlayerNumber pn, float delta) override;
  void SetLife(PlayerNumber pn, float value) override;
  void HandleTapScoreNone(PlayerNumber pn) override;

  bool IsInDanger() const;
  bool IsHot() const;
  bool IsFailing() const;
  float GetLife() const;
  LifeMeter* GetInnerLifeMeter() const;

 private:
  LifeMeter* m_pLifeMeter;
};

#endif
