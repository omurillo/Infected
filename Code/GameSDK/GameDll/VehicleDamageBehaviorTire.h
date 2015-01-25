/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2006.
-------------------------------------------------------------------------
$Id$
$DateTime$
Description: Implements a damage behavior which Tire stuff

-------------------------------------------------------------------------
History:
- 03:28:2006: Created by Mathieu Pinard

*************************************************************************/
#ifndef __VEHICLEDAMAGEBEHAVIORTIRE_H__
#define __VEHICLEDAMAGEBEHAVIORTIRE_H__

class CVehicle;

class CVehicleDamageBehaviorBlowTire
	: public IVehicleDamageBehavior
{
	IMPLEMENT_VEHICLEOBJECT
public:

	CVehicleDamageBehaviorBlowTire() {}
	virtual ~CVehicleDamageBehaviorBlowTire() {}

	virtual bool Init(IVehicle* pVehicle, const CVehicleParams& table);
	virtual void Reset();
	virtual void Release() { delete this; }

	virtual void OnDamageEvent(EVehicleDamageBehaviorEvent event, const SVehicleDamageBehaviorEventParams& behaviorParams);

	virtual void Serialize(TSerialize ser, EEntityAspects aspects);
	virtual void Update(const float deltaTime);
  virtual void OnVehicleEvent(EVehicleEvent event, const SVehicleEventParams& params);

	virtual void GetMemoryUsage(ICrySizer * s) const;

protected:
  void Activate(bool activate);

	IVehicle* m_pVehicle;
  string m_component;
	bool m_isActive;
};

#endif
