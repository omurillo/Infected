/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2005.
-------------------------------------------------------------------------
$Id$
$DateTime$
Description: Implements a base class for vehicle movements

-------------------------------------------------------------------------
History:
- 09:28:2005: Created by Mathieu Pinard

*************************************************************************/
#include "StdAfx.h"
#include "VehicleMovementBase.h"

#include "Game.h"
#include "GameCVars.h"

#include "IVehicleSystem.h"
#include <IGameTokens.h>
#include <IEffectSystem.h>
#include "GameUtils.h"
#include "VehicleClient.h"
#include "GamePhysicsSettings.h"

#define RUNSOUND_FADEIN_TIME 0.5f
#define RUNSOUND_FADEOUT_TIME 0.5f
#define WIND_MINSPEED 1.f
#define WIND_MAXSPEED 50.f

IGameTokenSystem* CVehicleMovementBase::m_pGameTokenSystem = 0;
IVehicleSystem* CVehicleMovementBase::m_pVehicleSystem = 0;
IActorSystem* CVehicleMovementBase::m_pActorSystem = 0;
float CVehicleMovementBase::m_sprintTime = 0.f;

const float	CVehicleMovementBase::ms_engineSoundIdleRatio = 0.2f, CVehicleMovementBase::ms_engineSoundOverRevRatio = 0.2f;

static const NetworkAspectType ASPECT_VEHICLE_CLOAKING	= eEA_GameClientStatic;
static const NetworkAspectType ASPECT_VEHICLE_PHANTOM	= eEA_GameClientStatic;

//------------------------------------------------------------------------
CVehicleMovementBase::CVehicleMovementBase()
: m_actorId(0),
  m_pVehicle(0),
  m_pEntity(0),
	m_speed(0.f),
  m_maxSpeed(0.f),  
  m_enginePos(ZERO),
  m_runSoundDelay(0.f),
  m_rpmScale(0.f),
  m_rpmPitchSpeed(0.f),
  m_speedRatio(0.f),
  m_speedRatioUnit(0.f),
	m_keepEngineOn(false),
  m_isEngineDisabled(false),
  m_bMovementProcessingEnabled(true),
	m_remotePilot(false),
  m_rpmScaleSgn(0.f),
  m_boost(false),
	m_wasBoosting(false),
  m_boostCounter(1.f),
  m_boostEndurance(10.f),
  m_boostStrength(1.f),
  m_boostRegen(10.f),
  m_lastMeasuredVel(ZERO),
  m_measureSpeedTimer(0.f),
  m_dampAngle(ZERO),
  m_dampAngVel(ZERO),
	m_pPaParams(NULL),
	m_engineDisabledTimerId(-1),
	m_engineDisabledFXId(-1),
	m_isExhaustActivated(false),
	m_engineStartingForceFeedbackFxId(InvalidForceFeedbackFxId),
	m_enginePoweredForceFeedbackFxId(InvalidForceFeedbackFxId),
	m_collisionForceFeedbackFxId(InvalidForceFeedbackFxId),
	m_localSpeed(ZERO),
	m_localAccel(ZERO),
	m_engineIgnitionTime(1.6f)
{ 
	m_pWind[0] = m_pWind[1] = NULL;
	m_ejectionTimer = 0.f;
	m_ejectionTimer0 = 0.f;
	m_ejectionDotProduct = -1.1f;

	memset(&m_physStatus[k_mainThread], 0, sizeof(m_physStatus[0]));
	memset(&m_physStatus[k_physicsThread], 0, sizeof(m_physStatus[0]));
}

//------------------------------------------------------------------------
CVehicleMovementBase::~CVehicleMovementBase()
{
	// environmental
	for (SEnvParticleStatus::TEnvEmitters::iterator it=m_paStats.envStats.emitters.begin(); it!=m_paStats.envStats.emitters.end(); ++it)
	{
		FreeEmitterSlot(it->slot);

		if (it->pGroundEffect)
		{
			it->pGroundEffect->Stop(true);
			delete it->pGroundEffect;
		}
	}

  if (m_pWind[0])  
  {
    gEnv->pPhysicalWorld->DestroyPhysicalEntity(m_pWind[0]);      
    gEnv->pPhysicalWorld->DestroyPhysicalEntity(m_pWind[1]);      
  }
}

//------------------------------------------------------------------------
bool CVehicleMovementBase::Init(IVehicle* pVehicle, const CVehicleParams& table)
{
	m_pVehicle = pVehicle;
	m_pEntity = pVehicle->GetEntity();

	m_pGameTokenSystem = g_pGame->GetIGameFramework()->GetIGameTokenSystem();
	m_pVehicleSystem = g_pGame->GetIGameFramework()->GetIVehicleSystem();
	m_pActorSystem = g_pGame->GetIGameFramework()->GetIActorSystem();  

	// init particles
	m_pPaParams = m_pVehicle->GetParticleParams();  
	InitExhaust();    

	for (int i=0; i<eVMA_Max; ++i)
		m_animations[i] = 0;

	if (CVehicleParams animsTable = table.findChild("Animations"))
	{
		if (animsTable.haveAttr("engine"))
			m_animations[eVMA_Engine] = m_pVehicle->GetAnimation(animsTable.getAttr("engine"));
	}

	table.getAttr("keepEngineOn", m_keepEngineOn);
	m_isEnginePowered = false;
	m_isEngineStarting = false;
	m_isEngineGoingOff = false;
	m_isExhaustActivated = false;

	m_engineStartup = 0.0f;
	m_damage = 0.0f;
	m_engineDisabledTimerId = m_engineDisabledFXId = -1;

	table.getAttr("engineIgnitionTime", m_engineIgnitionTime);
		

	// Read sound parameters.

	string	vehiclePath;

	string	engineProjectName = m_pEntity->GetClass()->GetName();

	string	functionalityProjectName = engineProjectName;

	if(CVehicleParams soundParams = table.findChild("SoundParams"))
	{
		CRY_ASSERT(m_pVehicle);

		const IVehicle::SSoundParams	&vehicleSoundParams = m_pVehicle->GetSoundParams();

		if(vehicleSoundParams.engineProjectName.length())
		{
			vehiclePath				= "sounds/";
			engineProjectName	= vehicleSoundParams.engineProjectName;
		}
		else
		{
			vehiclePath = "sounds/vehicles:";

			if(soundParams.haveAttr("eventGroup"))
			{
				engineProjectName = soundParams.getAttr("eventGroup");
			}
		}

		if(vehicleSoundParams.functionalityProjectName.length())
		{
			vehiclePath								= "sounds/";
			functionalityProjectName	= vehicleSoundParams.functionalityProjectName;
		}
		else
		{
			vehiclePath = "sounds/vehicles:";

			if(soundParams.haveAttr("eventGroup"))
			{
				functionalityProjectName = soundParams.getAttr("eventGroup");
			}
		}

		if(soundParams.haveAttr("engineSoundPosition"))
		{
			if(IVehicleHelper *pHelper = m_pVehicle->GetHelper(soundParams.getAttr("engineSoundPosition")))
			{
				m_enginePos = pHelper->GetVehicleSpaceTranslation();
			}
			else
			{
				m_enginePos.zero();
			}
		}

		soundParams.getAttr("rpmPitchSpeed", m_rpmPitchSpeed);

		soundParams.getAttr("runSoundDelay", m_runSoundDelay);

		if(m_runSoundDelay > 0.f)
		{
			if(m_engineIgnitionTime < m_runSoundDelay + RUNSOUND_FADEIN_TIME)
			{
				GameWarning("runSoundDelay (plus fade in time) is larger than engineIgnitionTime so you probably won't get the affect you're looking for");
			}
		}
	}

	// Initialise sound names.

	if(!engineProjectName.empty() && !functionalityProjectName.empty())
	{
		string	enginePrefix(vehiclePath);

		enginePrefix.append(engineProjectName);  

		enginePrefix.MakeLower();

		string	functionalityPrefix(vehiclePath);

		functionalityPrefix.append(functionalityProjectName);  

		functionalityPrefix.MakeLower();

		m_soundNames[eSID_Start]        = enginePrefix + ":start";
		m_soundNames[eSID_Run]          = enginePrefix + ":run";
		m_soundNames[eSID_Stop]         = enginePrefix + ":stop";
		m_soundNames[eSID_Ambience]     = enginePrefix + ":ambience";
		m_soundNames[eSID_Bump]         = functionalityPrefix + ":bump_on_road";
		m_soundNames[eSID_Splash]       = functionalityPrefix + ":bounce_on_waves";
		m_soundNames[eSID_Gear]         = functionalityPrefix + ":gear";
		m_soundNames[eSID_Slip]         = functionalityPrefix + ":slip";
		m_soundNames[eSID_Acceleration] = enginePrefix + ":acceleration";
		m_soundNames[eSID_Boost]        = functionalityPrefix + ":boost";
		m_soundNames[eSID_Damage]       = functionalityPrefix + ":damage";
	}

	m_pEntitySoundsProxy = crycomponent_cast<IEntitySoundProxyPtr>(m_pEntity->CreateProxy(ENTITY_PROXY_SOUND));
	assert(m_pEntitySoundsProxy.get());
	if (!GetSoundName(eSID_Run).empty())
	{	
		// Only always pre-cache if we're running the editor
		if (gEnv->IsEditor())
			gEnv->pSoundSystem->Precache(GetSoundName(eSID_Run).c_str(), FLAG_SOUND_DEFAULT_3D, FLAG_SOUND_PRECACHE_EVENT_DEFAULT);
	}

	if (IVehicleComponent* pComp = m_pVehicle->GetComponent("Hull"))
		m_damageComponents.push_back(pComp);

	if (IVehicleComponent* pComp = m_pVehicle->GetComponent("Engine"))
		m_damageComponents.push_back(pComp);

	if (CVehicleParams boostTable = table.findChild("Boost"))
	{
		boostTable.getAttr("endurance", m_boostEndurance);
		boostTable.getAttr("regeneration", m_boostRegen);
		boostTable.getAttr("strength", m_boostStrength);
	}

	if (CVehicleParams airDampTable = table.findChild("AirDamp"))
	{
		airDampTable.getAttr("dampAngle", m_dampAngle);
		airDampTable.getAttr("dampAngVel", m_dampAngVel);
	}

	if (CVehicleParams eject = table.findChild("Eject"))
	{
		float angle=0.f;
		eject.getAttr("timer", m_ejectionTimer0);
		eject.getAttr("maxTippingAngle", angle);
		if (angle == 0.f)
		{
			// Don't auto eject passengers
			m_ejectionDotProduct = -1.1f;
		}
		else
		{
			m_ejectionDotProduct = cosf(DEG2RAD(angle));
			m_pVehicle->SetUnmannedFlippedThresholdAngle(DEG2RAD(angle));
		}
	}

	// Initialise force feedback effect ids.

	IForceFeedbackSystem	*pForceFeedbackSystem = g_pGame->GetIGameFramework()->GetIForceFeedbackSystem();

	CRY_ASSERT(pForceFeedbackSystem);

	m_engineStartingForceFeedbackFxId	= pForceFeedbackSystem->GetEffectIdByName("vehicleEngineStarting");
	m_enginePoweredForceFeedbackFxId	= pForceFeedbackSystem->GetEffectIdByName("vehicleEnginePowered");
	m_collisionForceFeedbackFxId			= pForceFeedbackSystem->GetEffectIdByName("vehicleCollision");

	if(CVehicleParams forceFeedback = table.findChild("ForceFeedback"))
	{
		int	numberOfChildren = forceFeedback.getChildCount();

		for(int i = 0; i < numberOfChildren; ++ i)
		{
			CVehicleParams	params = forceFeedback.getChild(i);

			if(!strcmp(params.getTag(), "Params"))
			{
				string	event = params.getAttr("event");

				string	effect = params.getAttr("effect");

				if(event == "EngineStarting")
				{
					m_engineStartingForceFeedbackFxId = pForceFeedbackSystem->GetEffectIdByName(effect);
				}
				else if(event == "EnginePowered")
				{
					m_enginePoweredForceFeedbackFxId = pForceFeedbackSystem->GetEffectIdByName(effect);
				}
				else if(event == "Collision")
				{
					m_collisionForceFeedbackFxId = pForceFeedbackSystem->GetEffectIdByName(effect);
				}
			}
		}
	}

	// Reset boost.

	ResetBoost();

	return true;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::PostInit()
{
}

//------------------------------------------------------------------------
void CVehicleMovementBase::Physicalize()
{	 
  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::PostPhysicalize()
{	
	if (!m_paStats.envStats.initalized)    
		InitSurfaceEffects();

	if (IPhysicalEntity* pPhysics = GetPhysics())
	{
		// Mark this as a moving platform
		pe_params_foreign_data ppf;
		ppf.iForeignFlagsOR = PFF_MOVING_PLATFORM;
		pPhysics->SetParams(&ppf);

		// Mark this as Vehicle physics.
		g_pGame->GetGamePhysicsSettings()->AddCollisionClassFlags(*pPhysics, gcc_vehicle);
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::ResetInput()
{
	ResetBoost();

	m_movementAction.Clear();
}

//------------------------------------------------------------------------
void CVehicleMovementBase::InitExhaust()
{ 
	if(!m_pPaParams)
		return;

  for (int i=0; i < (int)m_pPaParams->GetExhaustParams()->GetExhaustCount(); ++i)
  {
    SExhaustStatus stat;
    stat.pHelper = m_pPaParams->GetExhaustParams()->GetHelper(i);
		if (stat.pHelper)
			m_paStats.exhaustStats.push_back(stat);    
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::Release()
{

}

//------------------------------------------------------------------------
void CVehicleMovementBase::Reset()
{
	m_speed = 0.f;
	m_localSpeed.zero();
	m_localAccel.zero();

	m_isProbablyDistant = 0;
	m_isProbablyVisible = 1;

	m_damage = 0.0f;

	m_isEngineDisabled = false;
	m_isEngineGoingOff = false;
	m_isEngineStarting = false;
	m_engineStartup = 0.0f;
	m_rpmScale = m_rpmScaleSgn = 0.f;
	m_engineDisabledTimerId = -1;
	m_isExhaustActivated = false;
	m_remotePilot = false;
	
	pe_status_pos psp;
	pe_status_dynamics psd;
	IPhysicalEntity* pPhysics = GetPhysics();
	if (!pPhysics || !pPhysics->GetStatus(&psp) || !pPhysics->GetStatus(&psd))
	{
		memset(&m_physStatus[k_mainThread], 0, sizeof(m_physStatus[0]));
		memset(&m_physStatus[k_physicsThread], 0, sizeof(m_physStatus[0]));
	}
	else
	{
		UpdatePhysicsStatus(&m_physStatus[k_mainThread], &psp, &psd);
		UpdatePhysicsStatus(&m_physStatus[k_physicsThread], &psp, &psd);
	}

	if (m_isEnginePowered)
	{
		m_isEnginePowered = false;
		OnEngineCompletelyStopped();		
	}

	StopSounds();  
	ResetParticles();  
	ResetBoost();

	for (int i=0; i<eVMA_Max; ++i)
	{
		if (m_animations[i])
			m_animations[i]->Reset();
	}

	if (m_pWind[0])  
	{
		gEnv->pPhysicalWorld->DestroyPhysicalEntity(m_pWind[0]);
		gEnv->pPhysicalWorld->DestroyPhysicalEntity(m_pWind[1]);
		m_pWind[0] = m_pWind[1] = 0;
	}

	//InitWind();

	m_movementAction.Clear();
	m_movementAction.brake = true;  
	m_bMovementProcessingEnabled = true;

	m_lastMeasuredVel.zero();
}

//------------------------------------------------------------------------
bool CVehicleMovementBase::RequestMovement(CMovementRequest& movementRequest)
{
  return false;
}

//------------------------------------------------------------------------

/*static*/ void CVehicleMovementBase::UpdatePhysicsStatus(SVehiclePhysicsStatus* status, const pe_status_pos* psp, const pe_status_dynamics* psd)
{
	status->pos = psp->pos;
	status->q = psp->q;
	status->v = psd->v;
	status->w = psd->w;
	status->centerOfMass = psd->centerOfMass;
	status->submergedFraction = psd->submergedFraction;
	status->mass = psd->mass;
}


//////////////////////////////////////////////////////////////////////////
// NOTE: This function must be thread-safe. Before adding stuff contact MarcoC.
void CVehicleMovementBase::ProcessMovement(const float deltaTime)
{
	FUNCTION_PROFILER( GetISystem(), PROFILE_GAME );

	IPhysicalEntity* pPhysics = GetPhysics();
	if (pPhysics==NULL)
		return;

	m_movementAction.isAI = false;

	pe_status_pos psp;
	pe_status_dynamics psd;

	if (!pPhysics->GetStatus(&psp) || !pPhysics->GetStatus(&psd))
		return;

	UpdatePhysicsStatus(&m_physStatus[k_physicsThread], &psp, &psd);

	IActor* pActor = m_pActorSystem->GetActor(m_actorId);
	
	if (pActor && pActor->IsPlayer())
	{
		m_movementAction.isAI = false;
		ProcessActions(deltaTime);
	}
	else if(pActor || m_remotePilot)
	{
		m_movementAction.isAI = true;
		ProcessAI(deltaTime); 
	}  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::RequestActions(const SVehicleMovementAction& movementAction)
{ 
	m_movementAction = movementAction;

	for (TVehicleMovementActionFilterList::iterator ite = m_actionFilters.begin(); ite != m_actionFilters.end(); ++ite)
	{
		IVehicleMovementActionFilter* pActionFilter = *ite;
		pActionFilter->OnProcessActions(m_movementAction);
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateSpeedRatio(const float deltaTime)
{ 
	const float speedNorm = m_speed / m_maxSpeed;
	m_speedRatioUnit = speedNorm;
	Interpolate(m_speedRatio, min(1.f, sqr( speedNorm ) ), 5.f, deltaTime);
}

//------------------------------------------------------------------------
void CVehicleMovementBase::Update(const float deltaTime)
{  
	FUNCTION_PROFILER( GetISystem(), PROFILE_GAME );

	IPhysicalEntity* pPhysics = GetPhysics();

	pe_status_pos psp;
	pe_status_dynamics psd;
	if (!pPhysics || !pPhysics->GetStatus(&psp) || !pPhysics->GetStatus(&psd))
		return;

	UpdatePhysicsStatus(&m_physStatus[k_mainThread], &psp, &psd);

	if(fabs(m_movementAction.power) > FLT_EPSILON || fabs(m_movementAction.rotateYaw) > FLT_EPSILON)
	{
		m_pVehicle->NeedsUpdate(IVehicle::eVUF_AwakePhysics);
	}

	// Cache current speed
	Vec3 localVel = m_physStatus[k_mainThread].v * m_physStatus[k_mainThread].q;
	m_speed = m_physStatus[k_mainThread].v.GetLength();
	m_localAccel += ((localVel - m_localSpeed)/(deltaTime+0.001f) - m_localAccel)*approxOneExp(10.f*deltaTime);
	m_localSpeed = localVel;
	UpdateSpeedRatio(deltaTime);    

	// Cache distance + visible status flags
	IGameObject* pGameObj = m_pVehicle->GetGameObject();
	m_isProbablyVisible = pGameObj->IsProbablyVisible() ? 1 : 0;
	m_isProbablyDistant = m_pVehicle->IsProbablyDistant() ? 1 : 0;

	const SVehicleStatus& status = m_pVehicle->GetStatus();

	int firstperson = 0;
	IActor* pClientActor = 0;

	if (m_pVehicle->IsPlayerPassenger())
	{
		pClientActor = g_pGame->GetIGameFramework()->GetClientActor();
		//if (!pClientActor->IsThirdPerson())
		IVehicleSeat* pSeat = (pClientActor != NULL) ? m_pVehicle->GetSeatForPassenger(pClientActor->GetEntityId()) : NULL;
		IVehicleView* pView = (pSeat != NULL) ? pSeat->GetView(pSeat->GetCurrentView()) : NULL;
		if ((pView != NULL) && !pView->IsThirdPerson())
			firstperson = 1;
	}

	// Check whether the ejection timer tripped
	if (m_ejectionTimer>m_ejectionTimer0)
	{
		m_ejectionTimer = 0.f;
		m_pVehicle->EvictAllPassengers();
	}

	UpdateDamage(deltaTime);
	UpdateBoost(deltaTime);

	if (gEnv->IsClient())
	{
		if(!m_pVehicle->IsPlayerDriving(true))
		{
			if(m_boost != m_wasBoosting)
				Boost(m_boost);
		}
		m_wasBoosting = m_boost;

		UpdateExhaust(deltaTime);
		UpdateSurfaceEffects(deltaTime);
		UpdateWind(deltaTime);
	}  

#if ENABLE_VEHICLE_DEBUG
	DebugDraw(deltaTime);
#endif

	if (m_isEngineStarting)
	{
		m_engineStartup += deltaTime;

		m_pVehicle->NeedsUpdate(IVehicle::eVUF_AwakePhysics); 

		if (m_runSoundDelay>0.f && m_engineStartup >= m_runSoundDelay)
		{
			ISound* pRunSound = GetSound(eSID_Run);
			if (!pRunSound)
			{
				// start run 
				pRunSound = PlaySound(eSID_Run, 0.f, m_enginePos);
				if (m_pVehicle->IsPlayerPassenger() && !GetSound(eSID_Ambience))
				{ 
					PlaySound(eSID_Ambience);          
				}
			}

			// "fade" in run and ambience

			float	fadeInRatio = min(1.f, (m_engineStartup-m_runSoundDelay)/RUNSOUND_FADEIN_TIME);

			m_rpmScale = fadeInRatio * ms_engineSoundIdleRatio;

			SetSoundParam(eSID_Run, "rpm_scale", m_rpmScale);
			SetSoundParam(eSID_Ambience, "speed", m_rpmScale);
			SetSoundParam(eSID_Ambience, "rpm_scale", m_rpmScale);

			if(m_pVehicle->IsPlayerPassenger())
			{
				if(m_engineStartingForceFeedbackFxId != InvalidForceFeedbackFxId)
				{
					CRY_ASSERT(g_pGame->GetIGameFramework()->GetIForceFeedbackSystem());

					g_pGame->GetIGameFramework()->GetIForceFeedbackSystem()->PlayForceFeedbackEffect(m_engineStartingForceFeedbackFxId, SForceFeedbackRuntimeParams(1.0f, 0.0f));
				}
			}
		}

		if (m_engineStartup >= m_engineIgnitionTime)		
		{
			OnEngineCompletelyStarted();
		}
	}
	else if (m_isEngineGoingOff)
	{
		m_engineStartup -= deltaTime;

		m_rpmScale = max(0.f, m_rpmScale - ms_engineSoundIdleRatio / RUNSOUND_FADEOUT_TIME * deltaTime);

		if (m_rpmScale <= ms_engineSoundIdleRatio && !GetSound(eSID_Stop))
		{
			// start stop sound and stop start sound, for now without fading
			StopSound(eSID_Start);      
			PlaySound(eSID_Stop, 0.f, m_enginePos);      
		}

		// handle run sound 
		if (m_rpmScale <= 0.f)
		{
			StopSound(eSID_Ambience);
			StopSound(eSID_Run);
			StopSound(eSID_Damage);
		}
		else      
		{ 
			SetSoundParam(eSID_Run, "rpm_scale", m_rpmScale);
		} 

		if (m_engineStartup <= 0.0f && m_rpmScale <= 0.f)
		{
			m_isEngineGoingOff = false;
			m_isEnginePowered = false;

			OnEngineCompletelyStopped();			
		}

		m_pVehicle->NeedsUpdate();
	}
	else if (m_isEnginePowered)
	{
		if (gEnv->IsClient())
		{
			if (!GetSound(eSID_Run))
				PlaySound(eSID_Run, 0.f, m_enginePos);

			UpdateRunSound(deltaTime);

			if(m_pVehicle->IsPlayerPassenger())
			{
				if(m_enginePoweredForceFeedbackFxId != InvalidForceFeedbackFxId)
				{
					CRY_ASSERT(g_pGame->GetIGameFramework()->GetIForceFeedbackSystem());

					g_pGame->GetIGameFramework()->GetIForceFeedbackSystem()->PlayForceFeedbackEffect(m_enginePoweredForceFeedbackFxId, SForceFeedbackRuntimeParams(m_rpmScale, 0.0f));
				}
			}
		}
	}

	m_soundStats.inout = 1.f;   
	if (firstperson)
	{ 
		if (IVehicleSeat* pSeat = m_pVehicle->GetSeatForPassenger(pClientActor->GetEntityId()))
			m_soundStats.inout = pSeat->GetSoundParams().inout;  
	}

	SetSoundParam(eSID_Run, "in_out", m_soundStats.inout);
	SetSoundParam(eSID_Ambience, "thirdperson", 1.f-firstperson);
  
	// Update Game Tokens
	if(m_pVehicle->IsPlayerDriving(true)||m_pVehicle->IsPlayerPassenger())
	{ 
		m_pGameTokenSystem->SetOrCreateToken("vehicle.rpmNorm", TFlowInputData(m_rpmScale, true));
		m_pGameTokenSystem->SetOrCreateToken("vehicle.speedNorm", TFlowInputData(m_speedRatioUnit, true));
	}    
}

int CVehicleMovementBase::GetStatus(SVehicleMovementStatus* _status)
{
	if (_status->typeId == eVMS_general)
	{
		SVehicleMovementStatusGeneral* status = (SVehicleMovementStatusGeneral*)_status;
		SVehiclePhysicsStatus* physStatus = &m_physStatus[k_mainThread];
		status->speed = m_speed;
		status->pos = physStatus->pos;
		status->q = physStatus->q;
		status->centerOfMass = physStatus->centerOfMass;
		status->vel = physStatus->v;
		status->angVel = physStatus->w;
		status->localAngVel = physStatus->w * physStatus->q;
		status->localAccel = m_localAccel;
		status->localVel = m_localSpeed;
		status->submergedRatio = physStatus->submergedFraction;
		status->topSpeed = m_maxSpeed;
	}
	return 0;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateRunSound(const float deltaTime)
{
	float radius = 200.0f;
	ISound* pEngineSound = GetSound(eSID_Run);

	if (pEngineSound)
		radius = MAX(radius, pEngineSound->GetMaxDistance());

	// not needed... ambient sound is much quieter than the engine sound; code is here for reference
	//	ISound* pAmbientSound = GetSound(eSID_Ambience);
	//	if (pAmbientSound)
	//		radius = std::max(radius, pAmbientSound->GetMaxDistance());

	if (IActor * pActor = g_pGame->GetIGameFramework()->GetClientActor())
	{
		// 1.1f is a small safety factor to get sound params updated before they're heard
		if (pActor->GetEntity()->GetWorldPos().GetDistance( m_pVehicle->GetEntity()->GetWorldPos() ) > radius*1.1f)
			return;
	}

	float soundSpeedRatio = ms_engineSoundIdleRatio + (1.f-ms_engineSoundIdleRatio) * m_speedRatio;      

	SetSoundParam(eSID_Run, "speed", soundSpeedRatio);
	SetSoundParam(eSID_Run, "abs_speed", m_speed);
	SetSoundParam(eSID_Ambience, "speed", soundSpeedRatio);
	SetSoundParam(eSID_Ambience, "abs_speed", m_speed);

	float damage = GetSoundDamage();
	if (damage > 0.1f)
	{ 
		if (ISound* pSound = GetOrPlaySound(eSID_Damage, 5.f, m_enginePos))
			SetSoundParam(pSound, "damage", damage);    
	}

	//SetSoundParam(eSID_Run, "boost", Boosting() ? 1.f : 0.f);

	if (m_rpmPitchSpeed>0.f)
	{
		// pitch rpm with pedal          
		float delta = GetEnginePedal() - m_rpmScaleSgn;
		m_rpmScaleSgn = max(-1.f, min(1.f, m_rpmScaleSgn + sgn(delta)*min(abs(delta), m_rpmPitchSpeed*deltaTime)));

		// skip transition around 0 when on pedal
		if (GetEnginePedal() != 0.f && delta != 0.f && sgn(m_rpmScaleSgn) != sgn(delta) && abs(m_rpmScaleSgn) <= 0.3f)
			m_rpmScaleSgn = sgn(delta)*0.3f;

		m_rpmScale = max(ms_engineSoundIdleRatio, abs(m_rpmScaleSgn) * (1.0f - ms_engineSoundOverRevRatio)); // PS - Not sure if we ever actually get here, but the calculation might be wrong.

		SetSoundParam(eSID_Run, "rpm_scale", m_rpmScale);
		SetSoundParam(eSID_Ambience, "rpm_scale", m_rpmScale);
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateDamage(const float deltaTime)
{
  const SVehicleStatus& status = m_pVehicle->GetStatus();
  const SVehicleDamageParams& damageParams = m_pVehicle->GetDamageParams();

  if (status.submergedRatio > damageParams.submergedRatioMax)
  { 
    if (m_damage < 1.f)
    {
      SetDamage(m_damage + deltaTime*damageParams.submergedDamageMult, true);

			if(m_pVehicle)
			{
				IVehicleComponent* pEngine = m_pVehicle->GetComponent("Engine");
				if(!pEngine)
					pEngine = m_pVehicle->GetComponent("engine");

				if(pEngine)
				{
          pEngine->SetDamageRatio(m_damage);

					// also send a zero damage event, to ensure things like the HUD update correctly
					SVehicleEventParams params;
					m_pVehicle->BroadcastVehicleEvent(eVE_Damaged, params);
				}
			}
    }
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::SetDamage(float damage, bool fatal)
{
  if (m_damage == 1.f)
    return;

  m_damage = min(1.f, max(m_damage, damage));

  if (m_damage == 1.f && fatal)
  {
    if (m_isEnginePowered || m_isEngineStarting)
      OnEngineCompletelyStopped();

		m_isEngineDisabled = true;
    m_isEnginePowered = false;
    m_isEngineStarting = false;
    m_isEngineGoingOff = false;

    m_movementAction.Clear();
    StopExhaust();
    StopSounds();
  }
}

//------------------------------------------------------------------------
float CVehicleMovementBase::GetSoundDamage()
{
  float damage = 0.f;
  
  for (TComponents::const_iterator it=m_damageComponents.begin(),end=m_damageComponents.end(); it!=end; ++it)  
    damage = max(damage, (*it)->GetDamageRatio());
  
  return damage;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::OnSoundEvent(ESoundCallbackEvent event,ISound *pSound)
{
}


//------------------------------------------------------------------------
bool CVehicleMovementBase::StartDriving( EntityId driverId )
{
	if (m_isEngineDisabled || m_pVehicle->IsDestroyed())
		return false;

	m_actorId = driverId;

	m_movementAction.Clear();
	m_movementAction.brake = false;

	return StartEngine();
}

bool CVehicleMovementBase::StartEngine()
{  
	if (m_isEngineDisabled || m_isEngineStarting || m_pVehicle->IsDestroyed())
		return false;

	// WarmupEngine relies on this being done here!
	if (m_isEnginePowered && !m_isEngineGoingOff)
	{ 
    StartExhaust(false, false);

		if (m_pVehicle->IsPlayerPassenger())
			GetOrPlaySound(eSID_Ambience);

		if (!m_isEngineGoingOff)
			return true;
	}

	m_isEngineGoingOff = false;
	m_isEngineStarting = true;
	m_engineStartup = 0.0f;
  m_rpmScale = 0.f;
	
  StopSound(eSID_Run);
  StopSound(eSID_Ambience);
  StopSound(eSID_Damage);
  StopSound(eSID_Stop);
  
  PlaySound(eSID_Start, 0.f, m_enginePos);
  
  StartExhaust();
  StartAnimation(eVMA_Engine);
  InitWind();
    
	m_pVehicle->GetGameObject()->EnableUpdateSlot(m_pVehicle, IVehicle::eVUS_EnginePowered);
    
	return true;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::StopDriving()
{
	m_actorId = 0;
	
	if(!m_keepEngineOn)
		StopEngine();
}

void CVehicleMovementBase::StopEngine()
{
	if (m_isEngineGoingOff || !m_isEngineStarting && !m_isEnginePowered)
		return;

	if (!m_isEngineStarting)
		m_engineStartup = m_engineIgnitionTime;

	m_isEngineStarting = false;
	m_isEngineGoingOff = true;

	m_movementAction.Clear();
	m_movementAction.brake = true;

	// Reset Game Tokens
	if(m_pVehicle->IsPlayerDriving(true)||m_pVehicle->IsPlayerPassenger())
	{
		m_pGameTokenSystem->SetOrCreateToken("vehicle.speedNorm", TFlowInputData(0.f, true));
		m_pGameTokenSystem->SetOrCreateToken("vehicle.rpmNorm", TFlowInputData(0.f, true));
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::OnEngineCompletelyStarted()
{
	m_isEngineStarting = false;
	m_isEnginePowered = true;
	m_engineStartup = m_engineIgnitionTime;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::OnEngineCompletelyStopped()
{
  m_pVehicle->GetGameObject()->DisableUpdateSlot(m_pVehicle, IVehicle::eVUS_EnginePowered);

  SVehicleEventParams params;
  m_pVehicle->BroadcastVehicleEvent(eVE_EngineStopped, params);

	StopExhaust();
	StopAnimation(eVMA_Engine);
}

//------------------------------------------------------------------------
void CVehicleMovementBase::DisableEngine(bool disable)
{
  if (disable == m_isEngineDisabled)
    return;

  if (disable)
  {
    if (m_isEnginePowered || m_isEngineStarting)
		{
			StopDriving();
			StopEngine();
		}

    m_isEngineDisabled = true;
  }
  else
  {
    m_isEngineDisabled = false;

    IVehicleSeat* pSeat = m_pVehicle->GetSeatById(1);
    if (pSeat && pSeat->GetPassenger() && pSeat->GetCurrentTransition()==IVehicleSeat::eVT_None)
		{
			StartDriving(pSeat->GetPassenger());
			StartEngine();
		}
		else if(m_keepEngineOn)
		{
			StartEngine();
		}
  }  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::OnAction(const TVehicleActionId actionId, int activationMode, float value)
{
	if (actionId == eVAI_RotatePitch)
		m_movementAction.rotatePitch = value;

	else if (actionId == eVAI_RotateYaw)
		m_movementAction.rotateRoll = value;

	else if (actionId == eVAI_MoveForward || actionId == eVAI_XIAccelerate)
	{
		m_movementAction.power = value;
	}
  else if (actionId == eVAI_MoveBack || actionId == eVAI_XIDeccelerate)
	{
		m_movementAction.power = -value;
	}
	else if (actionId == eVAI_TurnLeft)
		m_movementAction.rotateYaw = -value;

	else if (actionId == eVAI_TurnRight)
		m_movementAction.rotateYaw = value;

	else if (actionId == eVAI_Brake)
		m_movementAction.brake = (value > 0.0f);  

  else if (actionId == eVAI_Boost)
  { 
    if (!Boosting() && activationMode == eAAM_OnPress)
      Boost(true);    
    else if (activationMode == eAAM_OnRelease)
      Boost(false);   
  }

	if(fabs(m_movementAction.power) > FLT_EPSILON || fabs(m_movementAction.rotateYaw) > FLT_EPSILON)
	{
		m_pVehicle->NeedsUpdate(IVehicle::eVUF_AwakePhysics);
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::ResetBoost()
{  
  Boost(false);
  m_boostCounter = 1.f;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::Boost(bool enable)
{
  if ((enable == m_boost && m_pVehicle && m_pVehicle->IsPlayerDriving(true)) || m_boostEndurance == 0.f)
    return;
  
  if (enable)
  {
    if (m_boostCounter < 0.25f || m_movementAction.power < -0.001f /*|| m_damage == 1.f*/)
      return;

    if ((m_physStatus[k_mainThread].submergedFraction > 0.75f) && (GetMovementType()!=IVehicleMovement::eVMT_Amphibious))
      return; // can't boost if underwater and not amphibious
  }

	// NB: m_pPaParams *can* be null here (amphibious apc has two VehicleMovement objects and one isn't
	//	initialised fully the first time we get here)
  if(m_pPaParams)
	{
		const char* boostEffect = m_pPaParams->GetExhaustParams()->GetBoostEffect();  
		if (IParticleEffect* pBoostEffect = gEnv->pParticleManager->FindEffect(boostEffect))
		{
			SEntitySlotInfo slotInfo;
			for (std::vector<SExhaustStatus>::iterator it=m_paStats.exhaustStats.begin(), end=m_paStats.exhaustStats.end(); it!=end; ++it)
			{ 
				if (enable)
				{
					it->boostSlot = m_pEntity->LoadParticleEmitter(it->boostSlot, pBoostEffect);
	        
					if (m_pEntity->GetSlotInfo(it->boostSlot, slotInfo) && slotInfo.pParticleEmitter)
					{         
						Matrix34 tm;
						it->pHelper->GetVehicleTM(tm);
						m_pEntity->SetSlotLocalTM(it->boostSlot, tm);
					}      
				}
				else
				{        
					if (m_pEntity->GetSlotInfo(it->boostSlot, slotInfo) && slotInfo.pParticleEmitter)
						slotInfo.pParticleEmitter->Activate(false);
	        
					FreeEmitterSlot(it->boostSlot);
				}  
			}
		}
	}
	
  if (enable)
    PlaySound(eSID_Boost, 2.f, m_enginePos);
  
  m_boost = enable;  
}

//------------------------------------------------------------------------

void CVehicleMovementBase::EjectionTest(float deltaTime)
{
	// This function needs to be MT safe
	const Vec3 zAxis = m_physStatus[k_physicsThread].q.GetColumn2();
	if (zAxis.z > m_ejectionDotProduct)
	{
		m_ejectionTimer = 0.f;
	}
	else
	{
		m_ejectionTimer += deltaTime;
	}
}


//------------------------------------------------------------------------
void CVehicleMovementBase::ApplyAirDamp(float angleMin, float angVelMin, float deltaTime, int threadSafe)
{
	// This function need to be MT safe, called StdWeeled and Tank Hovercraft
	// flight stabilization, correct deviation from neutral orientation
	
	IPhysicalEntity* pPhysics = GetPhysics();
	if (!pPhysics)
		return;
	
	SVehiclePhysicsStatus* physStatus = &m_physStatus[k_physicsThread];

	if (m_movementAction.isAI || physStatus->submergedFraction>0.01f || m_dampAngle.IsZero())
		return;
		
	Matrix34 worldTM( physStatus->q );

	if (worldTM.GetColumn2().z < -0.05f)
		return;

	Ang3 angles(worldTM);
	Vec3 localW = physStatus->w * physStatus->q;

	Vec3 correction(ZERO);

	for (int i=0; i<3; ++i)
	{
		// angle correction
		if (i != 2)
		{
			bool increasing = sgn(localW[i])==sgn(angles[i]);
			if ((increasing || abs(angles[i]) > angleMin) && abs(angles[i]) < 0.45f*gf_PI)
				correction[i] += m_dampAngle[i] * -angles[i]; 
		}

		// angular velocity
		if (abs(localW[i]) > angVelMin)
			correction[i] += m_dampAngVel[i] * -localW[i];

		correction[i] *= physStatus->mass * deltaTime;
	}      

	if (!correction.IsZero())
	{
		// not thread-safe
		//if (IsProfilingMovement())
		//{
		//  float color[] = {1,1,1,1};
		//  gEnv->pRenderer->Draw2dLabel(300,500,1.4f,color,false,"corr: %.1f, %.1f", correction.x, correction.y);
		//}

		pe_action_impulse imp;
		imp.angImpulse = worldTM.TransformVector(correction);
		pPhysics->Action(&imp, threadSafe);
	}     
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateGravity(float grav)
{ 
	// set increased freefall gravity when airborne
	// this needs to be set per-frame (otherwise one needs to set ignore_areas) 
	// and from the immediate callback only

	IPhysicalEntity* pPhysics = GetPhysics();
	if (!pPhysics)
		return;

	pe_simulation_params params;
	if (pPhysics->GetParams(&params))
	{
		pe_simulation_params paramsSet;
		paramsSet.gravityFreefall = params.gravityFreefall;
		paramsSet.gravityFreefall.z = min(paramsSet.gravityFreefall.z, grav);      
		pPhysics->SetParams(&paramsSet, 1);
	}

}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateBoost(const float deltaTime)
{ 
  if (m_boost)
  {
    m_boostCounter = max(0.f, m_boostCounter - deltaTime/m_boostEndurance);

    if (m_boostCounter == 0.f || m_movementAction.power < -0.001f)
      Boost(false);
  }
  else
  {
    if (m_boostRegen != 0.f)
      m_boostCounter = min(1.f, m_boostCounter + deltaTime/m_boostRegen);
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::OnEvent(EVehicleMovementEvent event, const SVehicleMovementEventParams& params)
{
	if (event == eVME_Damage)
	{ 
    if (m_damage < 1.f && params.fValue > m_damage)
      SetDamage(params.fValue, params.bValue);
	}
	else if (eVME_Freeze == event)
	{
		if (params.fValue>0.0f)
		{
			DisableEngine(true);
		}
		else
			DisableEngine(false);
	}
  else if (event == eVME_Repair)
  {
		m_damage = max(0.f, min(params.fValue, m_damage));
		if(m_damage < 1.0f)
			m_isEngineDisabled = false;
  }
	else if (event == eVME_PlayerEnterLeaveVehicle)
	{
		if (params.bValue)
		{ 
			if (!GetSound(eSID_Ambience) && (m_isEnginePowered||m_isEngineStarting))
			{ 
        PlaySound(eSID_Ambience);				
        
        // fade in ambience with rpmscale
        SetSoundParam(eSID_Ambience, "rpm_scale", m_rpmScale); 
			}
		}
		else
		{	
      StopSound(eSID_Ambience);
		}    
	}
	else if (event == eVME_WarmUpEngine)
	{
		if (/*m_damage == 1.f || */m_pVehicle->IsDestroyed())
			return;

		StopSound(eSID_Start);

		OnEngineCompletelyStarted();

		// This Update is called to get the Run sound starting, but this should 
		// eventually be achieved in a nicer way
		
    GetOrPlaySound(eSID_Run, 0.f, m_enginePos);
		      
	  if (m_pVehicle->IsPlayerPassenger())
		  GetOrPlaySound(eSID_Ambience);		

		StartExhaust();
		StartAnimation(eVMA_Engine);

		m_pVehicle->GetGameObject()->EnableUpdateSlot(m_pVehicle, IVehicle::eVUS_EnginePowered);
  }
  else if (event == eVME_ToggleEngineUpdate)
  {
    if (!params.bValue && !IsPowered())
      RemoveSurfaceEffects();
  }
	else if (event == eVME_NW_Discharge)
	{

		DisableEngine(true);
		m_engineDisabledTimerId = m_pVehicle->SetTimer(-1,5000,this);
		if(params.sParam!=NULL && m_engineDisabledFXId==-1)
		{
			if(IParticleEffect* pEffect = gEnv->pParticleManager->FindEffect(params.sParam))
			{
				SpawnParams sp;
				sp.fSizeScale = params.fValue;
				m_engineDisabledFXId = m_pVehicle->GetEntity()->LoadParticleEmitter(-1,pEffect,&sp);
				Matrix34 offset;
				offset.SetIdentity();
				offset.SetTranslation(params.vParam);
				m_pVehicle->GetEntity()->SetSlotLocalTM(m_engineDisabledFXId,offset);
			}
		}	
	}
	else if(event == IVehicleMovement::eVME_Collision || event == IVehicleMovement::eVME_GroundCollision)
	{
		const float minCollisionImpulse = 2500.0f;
		const float maxCollisionImpulse = 40000.0f;

		if(params.fValue > minCollisionImpulse && m_pVehicle->IsPlayerDriving(true))
		{
			if(m_collisionForceFeedbackFxId != InvalidForceFeedbackFxId)
			{
				CRY_ASSERT(g_pGame->GetIGameFramework()->GetIForceFeedbackSystem());

				float intensity = GetClampedFraction(params.fValue, minCollisionImpulse, maxCollisionImpulse);
				g_pGame->GetIGameFramework()->GetIForceFeedbackSystem()->PlayForceFeedbackEffect(m_collisionForceFeedbackFxId, SForceFeedbackRuntimeParams(intensity, 0.0f));
			}
		}
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::OnVehicleEvent(EVehicleEvent event, const SVehicleEventParams& params)
{
	if(event==eVE_Timer)
	{
		if(params.iParam == m_engineDisabledTimerId)
		{
			DisableEngine(false);
			FreeEmitterSlot(m_engineDisabledFXId);
			m_engineDisabledFXId = -1;
		}
	}
}

//------------------------------------------------------------------------
tSoundID CVehicleMovementBase::GetSoundId(EVehicleMovementSound eSID)
{
  if (eSID<0 || eSID>=eSID_Max) 
    return INVALID_SOUNDID; 
  
  return m_soundStats.sounds[eSID]; 
}


//------------------------------------------------------------------------
ISound* CVehicleMovementBase::GetSound(EVehicleMovementSound eSID)
{
  assert(eSID>=0 && eSID<eSID_Max);

	if((eSID < 0) || (eSID >= eSID_Max) || (m_soundStats.sounds[eSID] == INVALID_SOUNDID))
    return 0;

  return m_pEntitySoundsProxy->GetSound(m_soundStats.sounds[eSID]);
}

//------------------------------------------------------------------------
void CVehicleMovementBase::StopSounds()
{
  m_surfaceSoundStats.Reset();

  for (int i=0; i<eSID_Max; ++i)
  {
    StopSound((EVehicleMovementSound)i);
  }	
}

//------------------------------------------------------------------------
void CVehicleMovementBase::StopSound(EVehicleMovementSound eSID, bool fade)
{
  assert(eSID>=0 && eSID<eSID_Max);

  if (m_soundStats.sounds[eSID] != INVALID_SOUNDID)
  {
    m_pEntitySoundsProxy->StopSound(m_soundStats.sounds[eSID], fade ? ESoundStopMode_EventFade: ESoundStopMode_AtOnce);
    m_soundStats.sounds[eSID] = INVALID_SOUNDID;    
  }
}


//------------------------------------------------------------------------
ISound* CVehicleMovementBase::PlaySound(EVehicleMovementSound eSID, float pulse, const Vec3& offset, int soundFlags)
{
  assert(eSID>=0 && eSID<eSID_Max);

	// verify - make all vehicle sound use obstruction and culling
	uint32 nSoundFlags = soundFlags | FLAG_SOUND_DEFAULT_3D;
  
  CTimeValue currTime = gEnv->pTimer->GetFrameStartTime();
  
  if ((currTime - m_soundStats.lastPlayed[eSID]).GetSeconds() >= pulse)
  {    
    const string& soundName = GetSoundName(eSID);
    
    if (!soundName.empty())
      m_soundStats.sounds[eSID] = m_pEntitySoundsProxy->PlaySound(soundName.c_str(), offset, Vec3Constants<float>::fVec3_OneY, nSoundFlags, 0, eSoundSemantic_Vehicle);
    
    m_soundStats.lastPlayed[eSID] = currTime;

    if (g_pGameCVars->v_debugSounds && !soundName.empty())
      CryLog("[%s] playing sound %s", m_pVehicle->GetEntity()->GetName(), soundName.c_str());
  }
  
  return GetSound(eSID);
}

//------------------------------------------------------------------------
ISound* CVehicleMovementBase::GetOrPlaySound(EVehicleMovementSound eSID, float pulse, const Vec3& offset, int soundFlags)
{
  assert(eSID>=0 && eSID<eSID_Max);

  if (ISound* pSound = GetSound(eSID))
  {
    if (pSound->IsPlaying())
      return pSound;    
  }
  
  return PlaySound(eSID, pulse, offset, soundFlags);
}

//------------------------------------------------------------------------
void CVehicleMovementBase::StartExhaust(bool ignition/*=true*/, bool reload/*=true*/)
{ 
	if(!m_pPaParams)
		return;

  SExhaustParams* exParams = m_pPaParams->GetExhaustParams();

  if (!exParams->hasExhaust)
    return;
    
	m_isExhaustActivated = true;    
  
  // start effect  
  if (ignition)
  {
    if (IParticleEffect* pEff = gEnv->pParticleManager->FindEffect(exParams->GetStartEffect()))    
    {
      for (std::vector<SExhaustStatus>::iterator it = m_paStats.exhaustStats.begin(); it != m_paStats.exhaustStats.end(); ++it)
      {
        if (GetWaterMod(*it))
        { 
          it->startStopSlot = m_pEntity->LoadParticleEmitter(it->startStopSlot, pEff);
					Matrix34 tm;
					it->pHelper->GetVehicleTM(tm);
          m_pEntity->SetSlotLocalTM(it->startStopSlot, tm);        
        }
      }
    }
  }
    
  // load emitters for exhaust running effect   
  if (IParticleEffect* pRunEffect = gEnv->pParticleManager->FindEffect(exParams->GetRunEffect()))
  { 
    SEntitySlotInfo slotInfo;
    SpawnParams spawnParams;
    spawnParams.fSizeScale = exParams->runBaseSizeScale;

    for (std::vector<SExhaustStatus>::iterator it = m_paStats.exhaustStats.begin(); it != m_paStats.exhaustStats.end(); ++it)
    { 
      if (reload || it->runSlot == -1 || !m_pEntity->IsSlotValid(it->runSlot))
        it->runSlot = m_pEntity->LoadParticleEmitter(it->runSlot, pRunEffect);
        
      if (m_pEntity->GetSlotInfo(it->runSlot, slotInfo) && slotInfo.pParticleEmitter)
      { 
        Matrix34 tm;
				it->pHelper->GetVehicleTM(tm);    
        m_pEntity->SetSlotLocalTM(it->runSlot, tm);
        slotInfo.pParticleEmitter->SetSpawnParams(spawnParams);
      }              
    }  
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::FreeEmitterSlot(int& slot)
{
  if (slot > -1)
  {
    //CryLogAlways("[VehicleMovement]: Freeing slot %i", slot);
    m_pEntity->FreeSlot(slot);
    slot = -1;
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::FreeEmitterSlot(const int& slot)
{
  if (slot > -1)  
    m_pEntity->FreeSlot(slot);     
}


//------------------------------------------------------------------------
void CVehicleMovementBase::StopExhaust()
{ 
	if(!m_pPaParams)
		return;

  SExhaustParams* exParams = m_pPaParams->GetExhaustParams();

  if (!exParams->hasExhaust) 
    return;
    
	m_isExhaustActivated = false;
	
	FreeExhaustParticles();
    
  // trigger stop effect if available
  if (0 != exParams->GetStopEffect()[0] && !m_pVehicle->IsDestroyed() /*&& m_damage < 1.f*/)
  {
    IParticleEffect* pEff = gEnv->pParticleManager->FindEffect(exParams->GetStopEffect());
    if (pEff)    
    { 
      for (std::vector<SExhaustStatus>::iterator it = m_paStats.exhaustStats.begin(); it != m_paStats.exhaustStats.end(); ++it)
      {
        if (GetWaterMod(*it))
        {          
          it->startStopSlot = m_pEntity->LoadParticleEmitter(it->startStopSlot, pEff);

					Matrix34 tm;
					it->pHelper->GetVehicleTM(tm);
          m_pEntity->SetSlotLocalTM(it->startStopSlot, tm);                  
          //CryLogAlways("[VehicleMovement::StopExhaust]: Loaded to slot %i..", it->startStopSlot);
        }
      }
    }
  }
}


//------------------------------------------------------------------------
void CVehicleMovementBase::FreeExhaustParticles()
{
	for (std::vector<SExhaustStatus>::iterator it = m_paStats.exhaustStats.begin(); it != m_paStats.exhaustStats.end(); ++it)
	{
		FreeEmitterSlot(it->boostSlot);
		FreeEmitterSlot(it->runSlot);
		FreeEmitterSlot(it->startStopSlot);
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::ResetParticles()
{   
  // exhausts
  for (std::vector<SExhaustStatus>::iterator it = m_paStats.exhaustStats.begin(); it != m_paStats.exhaustStats.end(); ++it)
  {
    FreeEmitterSlot(it->startStopSlot);
    FreeEmitterSlot(it->runSlot);
    FreeEmitterSlot(it->boostSlot);  
    it->enabled = 1;
  }  
	
	m_pPaParams->GetExhaustParams()->hasExhaust = true;
  
  RemoveSurfaceEffects();  

	if(m_pPaParams)
	{
		SEnvironmentParticles* pEnvParams = m_pPaParams->GetEnvironmentParticles();    

		SEnvParticleStatus::TEnvEmitters::iterator end = m_paStats.envStats.emitters.end();
		for (SEnvParticleStatus::TEnvEmitters::iterator it=m_paStats.envStats.emitters.begin(); it!=end; ++it)
		{
			//FreeEmitterSlot(it->slot);
			if (it->group >= 0)
			{
				const SEnvironmentLayer& layer = pEnvParams->GetLayer(it->layer);
				it->active = layer.IsGroupActive(it->group);      
			} 
		}
	}

	FreeEmitterSlot(m_engineDisabledFXId);

  //m_paStats.envStats.emitters.clear();
  //m_paStats.envStats.initalized = false;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::RemoveSurfaceEffects()
{
  for (SEnvParticleStatus::TEnvEmitters::iterator it=m_paStats.envStats.emitters.begin(); it!=m_paStats.envStats.emitters.end(); ++it)
    EnableEnvEmitter(*it, false);
}

//------------------------------------------------------------------------
void CVehicleMovementBase::EnableEnvEmitter(TEnvEmitter& emitter, bool enable)
{
  if (!enable)
  {
    if (emitter.slot > -1)
    {
      SEntitySlotInfo info;
      info.pParticleEmitter = 0;
      if (m_pEntity->GetSlotInfo(emitter.slot, info) && info.pParticleEmitter)
      { 
        SpawnParams sp;      
        sp.fCountScale = 0.f;
        info.pParticleEmitter->SetSpawnParams(sp);
      }
    }

    if (emitter.pGroundEffect)
      emitter.pGroundEffect->Stop(true);
  }  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateExhaust(const float deltaTime)
{ 
  FUNCTION_PROFILER( GetISystem(), PROFILE_GAME );

	if (m_isProbablyDistant | (m_isProbablyVisible^1)) return;

	if(!m_pPaParams)
		return;
  
  SExhaustParams* exParams = m_pPaParams->GetExhaustParams();
  
  if (exParams->hasExhaust)
  {
    SEntitySlotInfo info;
    SpawnParams sp;
    float countScale = 1;
    float sizeScale = 1;
		float speedScale = 1;
    
    float vel = m_speed;
    float absPower = abs(GetEnginePedal());

    // disable running exhaust if requirements aren't met
    if (vel < exParams->runMinSpeed || absPower < exParams->runMinPower || absPower > exParams->runMaxPower)         
    {   
      countScale = 0;          
    }
    else
    { 
      // scale with engine power
      float powerDiff = max(0.f, exParams->runMaxPower - exParams->runMinPower);        

      if (powerDiff)
      {
        float powerNorm = CLAMP(absPower, exParams->runMinPower, exParams->runMaxPower) / powerDiff;                                

        float countDiff = max(0.f, exParams->runMaxPowerCountScale - exParams->runMinPowerCountScale);          
        if (countDiff)          
          countScale *= (powerNorm * countDiff) + exParams->runMinPowerCountScale;

        float sizeDiff  = max(0.f, exParams->runMaxPowerSizeScale - exParams->runMinPowerSizeScale);
        if (sizeDiff)                      
          sizeScale *= (powerNorm * sizeDiff) + exParams->runMinPowerSizeScale;           

				float emitterSpeedDiff = max(0.f, exParams->runMaxPowerSpeedScale - exParams->runMinPowerSpeedScale);
				if(emitterSpeedDiff)
					speedScale *= (powerNorm * emitterSpeedDiff) + exParams->runMinPowerSpeedScale;
      }

      // scale with vehicle speed
      float speedDiff = max(0.f, exParams->runMaxSpeed - exParams->runMinSpeed);        

      if (speedDiff)
      {
        float speedNorm = CLAMP(vel, exParams->runMinSpeed, exParams->runMaxSpeed) / speedDiff;                                

        float countDiff = max(0.f, exParams->runMaxSpeedCountScale - exParams->runMinSpeedCountScale);          
        if (countDiff)          
          countScale *= (speedNorm * countDiff) + exParams->runMinSpeedCountScale;

        float sizeDiff  = max(0.f, exParams->runMaxSpeedSizeScale - exParams->runMinSpeedSizeScale);
        if (sizeDiff)                      
          sizeScale *= (speedNorm * sizeDiff) + exParams->runMinSpeedSizeScale;      

				float emitterSpeedDiff = max(0.f, exParams->runMaxSpeedSpeedScale - exParams->runMinSpeedSpeedScale);
				if(emitterSpeedDiff)
					speedScale *= (speedNorm * emitterSpeedDiff) + exParams->runMinSpeedSpeedScale;
      }
    }

    sp.fSizeScale = sizeScale;   
		sp.fSpeedScale = speedScale;

		if (exParams->disableWithNegativePower && GetEnginePedal() < 0.0f)
		{
			sp.fSizeScale *= 0.35f;
		}
            
		int numDisabled = 0;
    for (std::vector<SExhaustStatus>::iterator it = m_paStats.exhaustStats.begin(); it != m_paStats.exhaustStats.end(); ++it)
    {
      sp.fCountScale = (float)it->enabled * countScale * GetWaterMod(*it);

			if(it->enabled && it->pHelper)
			{
				if(IVehiclePart* pPart = it->pHelper->GetParentPart())
				{
					IVehiclePart::EVehiclePartState state = pPart->GetState();
					if(state == IVehiclePart::eVGS_Destroyed || state == IVehiclePart::eVGS_Damaged1)
					{
						it->enabled = 0;

						FreeEmitterSlot(it->boostSlot);
						FreeEmitterSlot(it->runSlot);
						FreeEmitterSlot(it->startStopSlot);

						continue;
					}
				}
			}
			else if(!it->enabled)
			{
				++numDisabled;

				if(numDisabled == m_paStats.exhaustStats.size())
				{
					// all exhausts disabled
					exParams->hasExhaust = false;
				}
					
				continue;
			}
            
      Matrix34 slotTM;
			if(it->pHelper)
				it->pHelper->GetVehicleTM(slotTM);
      
      if (m_pEntity->GetSlotInfo(it->runSlot, info) && info.pParticleEmitter)
      { 
        m_pVehicle->GetEntity()->SetSlotLocalTM(it->runSlot, slotTM);
        info.pParticleEmitter->SetSpawnParams(sp);
      }      

      if (Boosting() && m_pEntity->GetSlotInfo(it->boostSlot, info) && info.pParticleEmitter)
      { 
        m_pVehicle->GetEntity()->SetSlotLocalTM(it->boostSlot, slotTM);
        info.pParticleEmitter->SetSpawnParams(sp);
      }      
    }
    
#if ENABLE_VEHICLE_DEBUG
    if (DebugParticles())
    {
      IRenderer* pRenderer = gEnv->pRenderer;
      float color[4] = {1,1,1,1};
      float x = 200.f;

      pRenderer->Draw2dLabel(x,  80.0f, 1.5f, color, false, "Exhaust:");
      pRenderer->Draw2dLabel(x,  105.0f, 1.5f, color, false, "countScale: %.2f", sp.fCountScale);
      pRenderer->Draw2dLabel(x,  120.0f, 1.5f, color, false, "sizeScale: %.2f", sp.fSizeScale);
			pRenderer->Draw2dLabel(x,  135.0f, 1.5f, color, false, "speedScale: %.2f", sp.fSpeedScale);
    }
#endif
  }
}

//------------------------------------------------------------------------
float CVehicleMovementBase::GetWaterMod(SExhaustStatus& exStatus)
{  
  // check water flag
	const SVehicleStatus& status = m_pVehicle->GetStatus();
	bool inWater = status.submergedRatio > 0.05f;
  if ((inWater && !m_pPaParams->GetExhaustParams()->insideWater) || (!inWater && !m_pPaParams->GetExhaustParams()->outsideWater))
    return 0;

  return 1;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::RegisterActionFilter(IVehicleMovementActionFilter* pActionFilter)
{
	if (pActionFilter)
		m_actionFilters.push_back(pActionFilter);
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UnregisterActionFilter(IVehicleMovementActionFilter* pActionFilter)
{
	if (pActionFilter)
		m_actionFilters.remove(pActionFilter);
}

//------------------------------------------------------------------------
void CVehicleMovementBase::GetMovementState(SMovementState& movementState)
{
	movementState.minSpeed = 0.0f;
	movementState.normalSpeed = 15.0f;
	movementState.maxSpeed = 30.0f;
}

//------------------------------------------------------------------------
bool CVehicleMovementBase::GetStanceState(const SStanceStateQuery& query, SStanceState& state)
{
	return false;
}

//------------------------------------------------------------------------
bool CVehicleMovementBase::IsProfilingMovement()
{
	if (!m_pVehicle || !m_pVehicle->GetEntity() || g_pGameCVars->v_profileMovement==0)
		return false;

	if (m_pVehicle->IsPlayerDriving())
		return true;

	static ICVar* pDebugVehicle = gEnv->pConsole->GetCVar("v_debugVehicle");
	const char* debugVehicle = pDebugVehicle->GetString();
	const char* entityName = m_pVehicle->GetEntity()->GetName();

	if (debugVehicle[0]=='*')
	{
		return strstr(entityName, ++debugVehicle)!=NULL;
	}
	else
	{
		return (0 == strcmpi(debugVehicle, entityName));
	}
	return false;
}

//------------------------------------------------------------------------
SMFXResourceListPtr CVehicleMovementBase::GetEffectNode(int matId)
{
	if (matId <= 0)
		return 0;

	IMaterialEffects *mfx = g_pGame->GetIGameFramework()->GetIMaterialEffects();

	const char* mfxRow = m_pVehicle->GetParticleParams()->GetEnvironmentParticles()->GetMFXRowName();

	TMFXEffectId effectId = mfx->GetEffectId(mfxRow, matId);

	if (effectId != InvalidEffectId)
	{ 
		return mfx->GetResources(effectId);
	}
	else
	{
#if ENABLE_VEHICLE_DEBUG
		if (DebugParticles())
			CryLog("GetEffectString for %s -> %i failed", mfxRow, matId);
#endif
	}

	return 0;
}

//------------------------------------------------------------------------
const char* CVehicleMovementBase::GetEffectByIndex(int matId, const char* username)
{  
  SMFXResourceListPtr pResourceList = GetEffectNode(matId);
  if (!pResourceList.get())
    return 0;

  SMFXParticleListNode* pList = pResourceList->m_particleList;
   
  while (pList && pList->m_particleParams.userdata && 0 != strcmp(pList->m_particleParams.userdata, username))    
    pList = pList->pNext;      
  
  if (pList)
    return pList->m_particleParams.name;
   
  return 0;  
}

//------------------------------------------------------------------------
float CVehicleMovementBase::GetSurfaceSoundParam(int matId)
{
	SMFXResourceListPtr pResourceList = GetEffectNode(matId);
	
	if (pResourceList.get() && pResourceList->m_soundList)
	{
		const char* soundType = pResourceList->m_soundList->m_soundParams.name;
		if (soundType[0])
		{
			// Ask the vehicle system to map the surface-effect name to an FMOD index
			IVehicleSoundsRegistry* pSoundsRegistry = m_pVehicleSystem->GetSoundsRegistry();
			return pSoundsRegistry->GetFMODValue(soundType);
		}
	}

	return 0.f;  
}


//------------------------------------------------------------------------
void CVehicleMovementBase::InitSurfaceEffects()
{
  m_paStats.envStats.emitters.clear();

	if(!m_pPaParams)
		return;
    
  SEnvironmentParticles* envParams = m_pPaParams->GetEnvironmentParticles();

  for (int iLayer=0; iLayer < (int)envParams->GetLayerCount(); ++iLayer)
  {    
    const SEnvironmentLayer& layer = envParams->GetLayer(iLayer);
    
    int cnt = layer.GetHelperCount();
    if (layer.alignGroundHeight>0 || layer.alignToWater)
      cnt = max(1, cnt);
    
    m_paStats.envStats.emitters.reserve( m_paStats.envStats.emitters.size() + cnt);    
    
    for (int i=0; i<cnt; ++i)
    { 
      TEnvEmitter emitter;
      emitter.layer = iLayer;

      if (layer.alignGroundHeight>0)      
      { 
        // create ground effect if height specified
        IGroundEffect* pGroundEffect = g_pGame->GetIGameFramework()->GetIEffectSystem()->CreateGroundEffect(m_pEntity);

        if (pGroundEffect)
        {
          pGroundEffect->SetHeight(layer.alignGroundHeight);
          pGroundEffect->SetHeightScale(layer.maxHeightSizeScale, layer.maxHeightCountScale);
          pGroundEffect->SetFlags(pGroundEffect->GetFlags() | IGroundEffect::eGEF_StickOnGround);

		  const char* effectRow = m_pVehicle->GetParticleParams()->GetEnvironmentParticles()->GetMFXRowName(); 
		  pGroundEffect->SetInteraction(effectRow);
		            
          pGroundEffect->SetInteraction(effectRow);

          emitter.pGroundEffect = pGroundEffect;
          m_paStats.envStats.emitters.push_back(emitter);

#if ENABLE_VEHICLE_DEBUG
          if (DebugParticles())
            CryLog("<%s> Ground effect loaded with height %f", m_pVehicle->GetEntity()->GetName(), layer.alignGroundHeight);
#endif
        }
      }      
      else if (layer.alignToWater)
      {
        // else load emitter in slot                
        Matrix34 tm(IDENTITY);
        
        if ((int)layer.GetHelperCount() > i)
				{
					if (IVehicleHelper* pHelper = layer.GetHelper(i))
						pHelper->GetVehicleTM(tm);
				}
          
        emitter.slot = -1;
        emitter.quatT = QuatT(tm);
        m_paStats.envStats.emitters.push_back(emitter);
          
#if ENABLE_VEHICLE_DEBUG
        if (DebugParticles())
        {
          const Vec3 loc = tm.GetTranslation();
          CryLog("<%s> water-aligned emitter %i, local pos: %.1f %.1f %.1f", m_pVehicle->GetEntity()->GetName(), i, loc.x, loc.y, loc.z);          
        }              
#endif
      }
    }
  }

  m_paStats.envStats.initalized = true;  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::GetParticleScale(const SEnvironmentLayer& layer, float speed, float power, float& countScale, float& sizeScale, float& speedScale)
{
  if (speed < layer.minSpeed)
  {
    countScale = 0.f;
    return;
  }
  
  float speedDiff = max(0.f, layer.maxSpeed - layer.minSpeed);        

  if (speedDiff)
  {
    float speedNorm = CLAMP(speed, layer.minSpeed, layer.maxSpeed) / speedDiff;

    float countDiff = max(0.f, layer.maxSpeedCountScale - layer.minSpeedCountScale);          
    countScale *= (speedNorm * countDiff) + layer.minSpeedCountScale;

    float sizeDiff  = max(0.f, layer.maxSpeedSizeScale - layer.minSpeedSizeScale);
    sizeScale *= (speedNorm * sizeDiff) + layer.minSpeedSizeScale;  

		float emitterSpeedDiff = max(0.f, layer.maxSpeedSpeedScale - layer.minSpeedSpeedScale);
		speedScale *= (speedNorm * emitterSpeedDiff) + layer.minSpeedSpeedScale;
  }

  float countDiff = max(0.f, layer.maxPowerCountScale - layer.minPowerCountScale);          
  countScale *= (power * countDiff) + layer.minPowerCountScale;

  float sizeDiff  = max(0.f, layer.maxPowerSizeScale - layer.minPowerSizeScale);  
  sizeScale *= (power * sizeDiff) + layer.minPowerSizeScale;

	float emitterSpeedDiff = max(0.f, layer.maxPowerSpeedScale - layer.minPowerSpeedScale);
	speedScale *= (power * emitterSpeedDiff) + layer.minPowerSpeedScale;
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateSurfaceEffects(const float deltaTime)
{
  FUNCTION_PROFILER( GetISystem(), PROFILE_GAME );
  
  if (0 == g_pGameCVars->v_pa_surface)
  {
    ResetParticles();
    return;
  }

  const SVehicleStatus& status = m_pVehicle->GetStatus();
  if (status.speed < 0.01f)
    return;
  
  float distSq = m_pVehicle->GetEntity()->GetWorldPos().GetSquaredDistance(gEnv->pRenderer->GetCamera().GetPosition());
  if (distSq > sqr(300.f) || (distSq > sqr(50.f) && !m_isProbablyVisible))
    return;
  
  float powerNorm = CLAMP(abs(m_movementAction.power), 0.f, 1.f);
  
  SEnvironmentParticles* envParams = m_pPaParams->GetEnvironmentParticles();
  
  SEnvParticleStatus::TEnvEmitters::iterator end = m_paStats.envStats.emitters.end();
  for (SEnvParticleStatus::TEnvEmitters::iterator emitterIt = m_paStats.envStats.emitters.begin(); emitterIt!=end; ++emitterIt)  
  { 
    if (emitterIt->layer < 0)
    {
      assert(0);
      continue;
    }

    const SEnvironmentLayer& layer = envParams->GetLayer(emitterIt->layer);

    if (emitterIt->pGroundEffect)
    {
      float countScale = 1;
      float sizeScale = 1;    
			float speedScale = 1;

      if (!(m_isEngineGoingOff || m_isEnginePowered))
      {   
        countScale = 0;          
      }
      else
      { 
        GetParticleScale(layer, status.speed, powerNorm, countScale, sizeScale, speedScale);
      }

      emitterIt->pGroundEffect->Stop(false);
      emitterIt->pGroundEffect->SetBaseScale(sizeScale, countScale, speedScale);
      emitterIt->pGroundEffect->Update();      
    }   
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::InitWind()
{ 
  // disabled for now
  return;

  if(!GenerateWind())
    return;

  if (!m_pWind[0])
  {
    AABB bounds;
    m_pEntity->GetLocalBounds(bounds);

    primitives::box geomBox;
    geomBox.Basis.SetIdentity();    
    geomBox.bOriented = 0;    
    geomBox.size = bounds.GetSize(); // doubles size of entity bbox    
    geomBox.size.x *= 0.75f;
    geomBox.size.y = max(min(3.f, 0.5f*geomBox.size.y), 5.f);
    geomBox.size.z *= 0.75f;
    //geomBox.center.Set(0, -0.75f*geomBox.size.y, 0); // offset center to front
    geomBox.center.Set(0,0,0);

    IGeometry *pGeom1 = gEnv->pPhysicalWorld->GetGeomManager()->CreatePrimitive( primitives::box::type, &geomBox );    
    if (pGeom1)    
      m_pWind[0] = gEnv->pPhysicalWorld->AddArea( pGeom1, m_pEntity->GetWorldPos(), m_pEntity->GetRotation(), 1.f);

    IGeometry *pGeom2 = gEnv->pPhysicalWorld->GetGeomManager()->CreatePrimitive( primitives::box::type, &geomBox );    
    if (pGeom2)    
      m_pWind[1] = gEnv->pPhysicalWorld->AddArea( pGeom2, m_pEntity->GetWorldPos(), m_pEntity->GetRotation(), 1.f);    
  }

  if (m_pWind[0])    
  {
    pe_params_area area;
    area.bUniform = 0;
    //area.falloff0 = 0.8f;    
    m_pWind[0]->SetParams(&area);

    SetWind(Vec3(ZERO));  
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::UpdateWind(const float deltaTime)
{
  if (m_pVehicle->IsDestroyed())
    return;

  if (!m_pWind[0])
    return;

  if (m_speed > WIND_MINSPEED && (m_isProbablyVisible))
  {
    Vec3 p1, p2;
    GetWindPos(p1, p2);

    pe_params_pos pos;
    pos.q = m_pEntity->GetWorldRotation();
    
    pos.pos = p1;    
    m_pWind[0]->SetParams(&pos);        
    
    pos.pos = p2;
    m_pWind[1]->SetParams(&pos);
  }
  
  SetWind(m_physStatus[k_mainThread].v);  
}

//------------------------------------------------------------------------
Vec3 CVehicleMovementBase::GetWindPos(Vec3& posRad, Vec3& posLin)
{
  // wind area is shifted along -velocity by 2*distance from center to intersection with bounding box
  AABB bounds;
  m_pEntity->GetLocalBounds(bounds);
  Vec3 center = bounds.GetCenter();

  Vec3 vlocal = m_pEntity->GetWorldRotation().GetInverted() * m_physStatus[k_mainThread].v;
  vlocal.NormalizeFast();
    
  Ray ray;
  ray.origin = center - vlocal*(bounds.GetRadius()+0.1f);
  ray.direction = vlocal;

  Vec3 intersect;
  if (Intersect::Ray_AABB(ray, bounds, intersect))
  {
    posRad = center + 1.5f*(intersect - center);
    posLin = posRad + 2.f*(intersect - center);

    if (IsProfilingMovement())
    {
      IPersistantDebug* pDebug = g_pGame->GetIGameFramework()->GetIPersistantDebug();
      pDebug->Begin("GetWindPos", false);
      pDebug->AddSphere(m_pEntity->GetWorldTM()*posRad, 0.25f, ColorF(0,1,0,0.8f), 0.1f);
      pDebug->AddSphere(m_pEntity->GetWorldTM()*posLin, 0.25f, ColorF(0.5,1,0,0.8f), 0.1f);
    }

    posRad = m_pEntity->GetWorldTM() * posRad;
    posLin = m_pEntity->GetWorldTM() * posLin;

    //return m_pEntity->GetWorldTM() * posRad;
  } 
  
  return m_pEntity->GetWorldTM() * center;
}

//------------------------------------------------------------------------
void ClampWind(Vec3& wind)
{ 
  float min = g_pGameCVars->v_wind_minspeed;
  float len2 = wind.len2();
  
  if (len2 < min*min)
    wind.SetLength(min);
  else if (len2 <= WIND_MINSPEED*WIND_MINSPEED)
    wind.zero();
  else if (len2 > WIND_MAXSPEED*WIND_MAXSPEED)  
    wind *= WIND_MAXSPEED/wind.len();  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::SetWind(const Vec3& wind)
{  
  pe_params_buoyancy buoy;
  buoy.iMedium = 1;
  buoy.waterDensity = buoy.waterResistance = 0;  
  buoy.waterPlane.n.Set(0,0,-1);
  buoy.waterPlane.origin.Set(0,0,gEnv->p3DEngine->GetWaterLevel());    
  
  float min = g_pGameCVars->v_wind_minspeed;
    
  float radial = max(min, wind.len());
  if (radial < WIND_MINSPEED)
    radial = 0.f;

  // radial wind
  buoy.waterFlow.Set(0,0,-radial);    
  //ClampWind(buoy.waterFlow);
  m_pWind[0]->SetParams(&buoy); 
  
  // linear wind  
  buoy.waterFlow = 0.8f*wind;    
  ClampWind(buoy.waterFlow);
  m_pWind[1]->SetParams(&buoy);
  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::StartAnimation(EVehicleMovementAnimation eAnim)
{
  IVehicleAnimation* pAnim = m_animations[eAnim];
  
  if (pAnim)  
    pAnim->StartAnimation();  
}

//------------------------------------------------------------------------
void CVehicleMovementBase::StopAnimation(EVehicleMovementAnimation eAnim)
{
  IVehicleAnimation* pAnim = m_animations[eAnim];
  
  if (pAnim)  
    pAnim->StopAnimation();
}

//------------------------------------------------------------------------
void CVehicleMovementBase::SetAnimationSpeed(EVehicleMovementAnimation eAnim, float speed)
{
  IVehicleAnimation* pAnim = m_animations[eAnim];

  if (pAnim)
    pAnim->SetSpeed(speed);
}

//------------------------------------------------------------------------
const string& CVehicleMovementBase::GetSoundName(EVehicleMovementSound eSID)
{
  assert(eSID>=0 && eSID<eSID_Max);

  return m_soundNames[eSID];
}

//------------------------------------------------------------------------
void CVehicleMovementBase::SetSoundParam(EVehicleMovementSound eSID, const char* param, float value)
{
	if (ISound* pSound = GetSound(eSID))
	{ 
		pSound->SetParam(param, value, false);
	}		
}

//------------------------------------------------------------------------
void CVehicleMovementBase::SetSoundParam(ISound* pSound, const char* param, float value)
{
  pSound->SetParam(param, value, false);  
}

#if ENABLE_VEHICLE_DEBUG
//------------------------------------------------------------------------
void CVehicleMovementBase::DebugDraw(const float deltaTime)
{
  float color[] = {1,1,1,1};    
  static ICVar* pDebugVehicle = gEnv->pConsole->GetCVar("v_debugVehicle");
  float val = 0.f;
  float size = 1.4f;
  float y = 75.0f;
  float step = 20.0f;

  while (g_pGameCVars->v_debugSounds)
  {
    if (!m_pVehicle->IsPlayerPassenger() && 0!=strcmpi(m_pVehicle->GetEntity()->GetName(), pDebugVehicle->GetString()))
      break;

    gEnv->pRenderer->Draw2dLabel(500.0f,y,1.5f,color,false,"vehicle rpm: %.2f", m_rpmScale);

    if (ISound* pSound = GetSound(eSID_Run))
    { 
      if (pSound->GetParam("rpm_scale", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run rpm_scale: %.2f", val);

      if (pSound->GetParam("load", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run load: %.2f", val);

      if (pSound->GetParam("speed", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run speed: %.2f", val);

      if (pSound->GetParam("abs_speed", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run abs_speed: %.2f", val);

      if (pSound->GetParam("acceleration", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run acceleration: %.1f", val);

      if (pSound->GetParam("surface", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run surface: %.2f", val);

      if (pSound->GetParam("scratch", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run scratch: %.0f", val);

      if (pSound->GetParam("slip", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run slip: %.1f", val);

      if (pSound->GetParam("in_out", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run in_out: %.1f", val);

      if (pSound->GetParam("damage", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run damage: %.2f", val);

      if (pSound->GetParam("swim", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":run swim: %.2f", val);
    }

    if (ISound* pSound = GetSound(eSID_Ambience))
    { 
      gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,"-----------------------------");

      if (pSound->GetParam("rpm_scale", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":ambience rpm_scale: %.2f", val);

      if (pSound->GetParam("speed", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":ambience speed: %.2f", val);

      if (pSound->GetParam("abs_speed", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":ambience abs_speed: %.2f", val);

      if (pSound->GetParam("thirdperson", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":ambience thirdperson: %.1f", val);
    }

    if (ISound* pSound = GetSound(eSID_Slip))
    { 
      gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,"-----------------------------");

      if (pSound->GetParam("slip_speed", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":slip slip_speed: %.2f", val);

      if (pSound->GetParam("surface", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":slip surface: %.2f", val);
    }

    if (ISound* pSound = GetSound(eSID_Bump))
    { 
      gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,"-----------------------------");

      if (pSound->GetParam("intensity", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":bump intensity: %.2f", val);
    }

    if (ISound* pSound = GetSound(eSID_Splash))
    { 
      gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,"-----------------------------");

      if (pSound->GetParam("intensity", &val, false) != -1)
        gEnv->pRenderer->Draw2dLabel(500.0f,y+=step,size,color,false,":splash intensity: %.2f", val);
    }
    
    break;
  }

  while (g_pGameCVars->v_sprintSpeed != 0.f)
  { 
    if (!m_pVehicle->IsPlayerPassenger() && 0!=strcmpi(m_pVehicle->GetEntity()->GetName(), pDebugVehicle->GetString()))
      break;

    float speed = m_pVehicle->GetStatus().speed;
//    float* col = color;
            
    if (speed < 0.2f)
      m_sprintTime = 0.f;
    else if (speed*3.6f < g_pGameCVars->v_sprintSpeed)    
      m_sprintTime += deltaTime;      
//    else
//      col = red;
    
    gEnv->pRenderer->Draw2dLabel(400.0f, 300.0f, 1.5f, color, false, "t: %.2f", m_sprintTime);

    break;
  }  
}
#endif

//------------------------------------------------------------------------
void CVehicleMovementBase::Serialize(TSerialize ser, EEntityAspects aspects)
{
	// SVehicleMovementAction m_movementAction;

	if (ser.GetSerializationTarget() != eST_Network)
	{
    ser.BeginGroup("MovementBase");
    ser.Value("movementProcessingEnabled", m_bMovementProcessingEnabled);
		ser.Value("isEngineDisabled", m_isEngineDisabled);
		ser.Value("DriverId", m_actorId);
		ser.Value("damage", m_damage);    
    ser.Value("engineStartup", m_engineStartup);    
    ser.Value("isEngineGoingOff", m_isEngineGoingOff);
    ser.Value("boostCounter", m_boostCounter);
    
    ser.Value("isExhaustActivated", m_isExhaustActivated );

    bool isEngineStarting = m_isEngineStarting;
    ser.Value("isEngineStarting", isEngineStarting);
    
    bool isEnginePowered = m_isEnginePowered;
    ser.Value("isEnginePowered", isEnginePowered);
		
		if (ser.IsReading())
		{
			ResetParticles();

      m_pEntity = m_pVehicle->GetEntity();
      
      for (int i=0; i<eSID_Max; ++i)
        m_soundStats.lastPlayed[i].SetSeconds((int64)-100);

      if (!m_isEnginePowered && (isEnginePowered || (isEngineStarting && !m_isEngineStarting)))
      { 
        m_pVehicle->GetGameObject()->EnableUpdateSlot(m_pVehicle, IVehicle::eVUS_EnginePowered);        
        InitWind();        
      }
      else if (!isEnginePowered && !isEngineStarting && (m_isEnginePowered || m_isEngineStarting))
      {
        m_pVehicle->GetGameObject()->DisableUpdateSlot(m_pVehicle, IVehicle::eVUS_EnginePowered);
        StopSound(eSID_Run);
        StopSound(eSID_Damage);
      }

      m_isEnginePowered = isEnginePowered;
      m_isEngineStarting = isEngineStarting;
      if (!m_isExhaustActivated)
				FreeExhaustParticles();
		}
    
    m_paStats.Serialize(ser, aspects);
    m_surfaceSoundStats.Serialize(ser, aspects);

    ser.EndGroup();
	}
}

//------------------------------------------------------------------------
void CVehicleMovementBase::PostSerialize()
{
  m_movementAction.Clear();

  if (m_isEnginePowered || m_isEngineStarting)
  {
    PlaySound(eSID_Run, 0.f, m_enginePos);

    if (m_pVehicle->IsPlayerPassenger())
      GetOrPlaySound(eSID_Ambience);    
  }
}

//------------------------------------------------------------------------
void CVehicleMovementBase::ProcessEvent(SEntityEvent& event)
{ 
}

DEFINE_VEHICLEOBJECT(CVehicleMovementBase)

//--------------------------------------------------------------------------------------------
float SPID::Update( float inputVal, float setPoint, float clampMin, float clampMax )
{
	float	pError = setPoint - inputVal;
	float	output = m_kP * pError - m_kD * (pError - m_prevErr) + m_kI * m_intErr;
	m_prevErr = pError;

	// Accumulate integral, or clamp.
	if( output > clampMax )
		output = clampMax;
	else if( output < clampMin )
		output = clampMin;
	else
		m_intErr += pError;

	return output;
}

//--------------------------------------------------------------------------------------------
void SPID::Serialize(TSerialize ser)
{
  ser.Value("m", m_intErr);
  ser.Value("m_kD", m_kD);
  ser.Value("m_kI", m_kI);
  ser.Value("m_prevErr", m_prevErr);
}
