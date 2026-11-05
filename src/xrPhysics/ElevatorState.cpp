#include "stdafx.h"
#include "ElevatorState.h"
#include "IClimableObject.h"
#include "PHCharacter.h"
#include "MathUtils.h"
#include "PHWorld.h"
#include "../xrengine/device.h"
#include "../xrcdb/xr_area.h"
#include "../xrEngine/gamemtllib.h"
#ifdef DEBUG
#include "../xrEngine/Statgraph.h"
#include "debug_output.h"
#endif

static const float getting_on_dist = 0.3f;
static const float getting_out_dist = 0.4f;
static const float start_climbing_dist = 0.f;
static const float stop_climbing_dist = 0.1f;
static const float out_dist = 1.5f;

static const float look_angle_cosine = 0.9238795f; //22.5
static const float lookup_angle_sine = 0.34202014f; //20
extern class CPHWorld* ph_world;

// Ladder fixes for the actor (Gunsmith Docs research/37); stalkers and monsters keep vanilla's rules:
// - W climbs up and S down, wherever the camera looks (vanilla: W climbs down looking 20 degrees below level);
// - only near-pure sideways input, held a moment, steps off the ladder (vanilla: 45 degrees off, at once);
// - the climb force runs along the ladder (vanilla: 45 degrees into its box), with no idle pull into it.
static const float strafe_depart_cosine = 0.34202014f; // input within 20 degrees of the ladder's plane
static const float strafe_depart_time = 0.25f; // seconds
static const float force_side_k = 4.f; // sideways centring per metre off the axis
static const float force_side_max = 0.5f;
static const float force_into = 0.2f; // what is left of the push into the ladder

// The actor's exit at the top (research/37 section 6): climbing up near the top, rays look for a floor beside the
// ladder's top that the standing body fits on, with a clear way over the lip; the body is then carried up and over
// onto it (velocity set each physics step, collision on). Without such a landing vanilla's way stays: gravity back on
// at the top and the walking step-up over the lip.
static const float exit_zone = 0.35f; // looked for once the foot centre is this far below the ladder's top
static const float exit_probe_interval = 0.1f; // seconds between looks
static const float exit_gap = 0.25f; // the landing's column is this far past the ladder box, beyond the foot radius
static const float exit_rise_max = 1.6f; // the floor at most this far above the feet ...
static const float exit_drop_max = 1.0f; // ... and at most this far below them
static const float exit_lift = 0.06f; // the feet this far above the landing floor
static const float exit_stand = 1.8f; // the standing body's height
static const float exit_floor_cos = 0.7f; // the landing floor's slope at most about 45 degrees
static const float exit_speed = 2.4f; // metres per second along the way
static const float exit_time_max = 2.f; // seconds before an exit gives up in any case
static const float exit_stall_max = 0.25f; // seconds of hardly moving (a prop, a door in the way) before it gives up

static bool ladder_actor(CPHCharacter* character)
{
	return character && character->RestrictionType() == rtActor;
}

CElevatorState::CElevatorState()
{
	m_state = clbNoLadder;
	m_ladder = NULL;
	m_character = NULL;
	m_step = 0.f;
	m_strafe_time = 0.f;
	m_exit = false;
	m_exit_leg = 0;
	m_exit_rise.set(0.f, 0.f, 0.f);
	m_exit_land.set(0.f, 0.f, 0.f);
	m_exit_time = 0.f;
	m_exit_probe = 0.f;
	m_exit_stall = 0.f;
	m_exit_last.set(0.f, 0.f, 0.f);
}

// The nearest static hit that isn't passable or a ladder (bushes, ladder rails); its triangle's normal on request
static bool exit_ray(const Fvector& from, const Fvector& dir, float range, float& dist, Fvector* normal = NULL)
{
	CObjectSpace& space = inl_ph_world().ObjectSpace();
	collide::rq_result R;
	Fvector pos = from;
	float left = range, done = 0.f;
	while (left > EPS_L && space.RayPick(pos, dir, left, collide::rqtStatic, R, NULL))
	{
		CDB::TRI* T = space.GetStaticTris() + R.element;
		SGameMtl* m = GMLibrary().GetMaterialByIdx((u16)T->material);
		if (!m->Flags.test(SGameMtl::flPassable) && !m->Flags.test(SGameMtl::flClimable))
		{
			dist = done + R.range;
			if (normal)
			{
				const Fvector* V = space.GetStaticVerts();
				Fvector e1, e2;
				e1.sub(V[T->verts[1]], V[T->verts[0]]);
				e2.sub(V[T->verts[2]], V[T->verts[0]]);
				normal->crossproduct(e1, e2).normalize_safe();
				if (normal->dotproduct(dir) > 0.f) normal->invert();
			}
			return true;
		}
		const float skip = R.range + EPS_L;
		pos.mad(dir, skip);
		left -= skip;
		done += skip;
	}
	return false;
}

bool CElevatorState::FindExit()
{
	Fvector foot, to_upper, axis, side, norm;
	m_character->GetFootCenter(foot);
	const float r = m_character->FootRadius();
	const float to_up = m_ladder->DDUpperP(m_character, to_upper);
	Fvector upper = Fvector().mad(foot, to_upper, to_up); // the top of the ladder box's front face
	m_ladder->DDAxis(axis);
	const float half_side = m_ladder->DDSide(side);
	const float half_thick = m_ladder->DDNorm(norm);
	if (norm.dotproduct(Fvector().sub(foot, upper)) < 0.f) norm.invert(); // toward the climber
	side.y = 0.f;
	side.normalize_safe();
	norm.y = 0.f;
	norm.normalize_safe();
	const Fvector top = Fvector().mad(upper, norm, -half_thick); // the top of the box's centre line
	const float feet = foot.y - r;
	const Fvector up = {0.f, 1.f, 0.f}, down = {0.f, -1.f, 0.f};

	// over the top first, then either side, then out in front (a hatch)
	struct SCand { Fvector dir; float reach; } cands[4] = {
		{Fvector().invert(norm), half_thick + r + exit_gap},
		{side, half_side + r + exit_gap},
		{Fvector().invert(side), half_side + r + exit_gap},
		{norm, half_thick + 2.f * r + exit_gap},
	};
	for (const SCand& c : cands)
	{
		Fvector col = Fvector().mad(top, c.dir, c.reach);
		// the floor under the column
		const Fvector start = {col.x, feet + exit_rise_max + 0.3f, col.z};
		float dist;
		Fvector n;
		if (!exit_ray(start, down, exit_rise_max + exit_drop_max + 0.3f, dist, &n) || n.y < exit_floor_cos)
			continue;
		const float floor = start.y - dist;
		if (floor > feet + exit_rise_max || floor < feet - exit_drop_max)
			continue;
		// the standing body fits on it
		bool fits = true;
		const Fvector offs[5] = {
			{0.f, 0.f, 0.f}, Fvector().mul(norm, 0.8f * r), Fvector().mul(norm, -0.8f * r),
			Fvector().mul(side, 0.8f * r), Fvector().mul(side, -0.8f * r)
		};
		for (const Fvector& o : offs)
		{
			const Fvector from = {col.x + o.x, floor + 0.05f, col.z + o.z};
			if (exit_ray(from, up, exit_stand, dist))
			{
				fits = false;
				break;
			}
		}
		if (!fits) continue;
		// the way: up beside the ladder, then across over the lip
		const float land_y = floor + exit_lift + r; // the foot centre on the landing
		const float rise = land_y - foot.y;
		if (rise > 0.f && exit_ray(Fvector().set(foot.x, foot.y + exit_stand - 2.f * r, foot.z), up, rise, dist))
			continue;
		Fvector across = {col.x - foot.x, 0.f, col.z - foot.z};
		const float across_len = across.magnitude();
		if (across_len < EPS_L) continue;
		across.div(across_len);
		const float heights[3] = {exit_lift + 0.1f, 0.9f, exit_stand - 0.2f};
		bool clear = true;
		for (float h : heights)
		{
			const Fvector from = {foot.x, floor + h, foot.z};
			if (exit_ray(from, across, across_len, dist))
			{
				clear = false;
				break;
			}
		}
		if (!clear) continue;
		m_exit_rise.set(foot.x, land_y, foot.z);
		m_exit_land.set(col.x, land_y, col.z);
		return true;
	}
	return false;
}

void CElevatorState::UpdateExit()
{
	m_exit_time += m_step;
	Fvector foot;
	m_character->GetFootCenter(foot);
	if (m_exit_time > m_step * 1.5f && foot.distance_to(m_exit_last) < exit_speed * m_step * 0.25f)
		m_exit_stall += m_step;
	else
		m_exit_stall = 0.f;
	m_exit_last.set(foot);
	const Fvector& goal = m_exit_leg == 0 ? m_exit_rise : m_exit_land;
	Fvector d = Fvector().sub(goal, foot);
	const float dist = d.magnitude();
	if (dist < 0.02f)
	{
		if (m_exit_leg == 0)
		{
			m_exit_leg = 1;
			return;
		}
		dBodySetLinearVel(m_character->get_body(), 0.f, 0.f, 0.f);
		m_exit = false;
		SwitchState(clbNoLadder);
		return;
	}
	if (m_exit_time > exit_time_max || m_exit_stall > exit_stall_max)
	{
		// blocked (a prop, a door): vanilla's way from here
		dBodySetLinearVel(m_character->get_body(), 0.f, 0.f, 0.f);
		m_exit = false;
		SwitchState(clbNearUp);
		return;
	}
	// ease in over the first 0.15 s
	float speed = exit_speed * _min(1.f, 0.3f + m_exit_time / 0.15f);
	if (m_step > EPS_S) speed = _min(speed, dist / m_step);
	d.mul(speed / dist);
	dBodySetLinearVel(m_character->get_body(), d.x, d.y, d.z);
}

float CElevatorState::ClimbDirection()
{
	VERIFY(m_ladder&&m_character);
	Fvector d;
	m_ladder->DToPlain(m_character, d);
	float dir = m_character->ControlAccel().dotproduct(d);
	if (dir > EPS_L && !ladder_actor(m_character))dir *= (m_character->CamDir().y + lookup_angle_sine);
	return dir;
}

bool CElevatorState::StrafeDepart(float to_ax, const Fvector& ca, float control_a)
{
	if (fis_zero(to_ax) || fis_zero(control_a))
	{
		m_strafe_time = 0.f;
		return false;
	}
	float into = _abs(-ca.dotproduct(Fvector(m_ladder->Norm()).normalize()));
	if (!ladder_actor(m_character))
		return into < M_SQRT1_2;
	if (into >= strafe_depart_cosine)
	{
		m_strafe_time = 0.f;
		return false;
	}
	m_strafe_time += m_step;
	return m_strafe_time >= strafe_depart_time;
}

void CElevatorState::ClimbForceDir(Fvector& dir, float sign)
{
	// Along the axis, a pull back onto the axis sideways, and a little push into the ladder
	Fvector axis, side, norm, to_axis;
	m_ladder->DDAxis(axis);
	m_ladder->DDSide(side);
	m_ladder->DDNorm(norm);
	float to_ax = m_ladder->DDToAxis(m_character, to_axis);
	to_axis.mul(to_ax);
	Fvector pull;
	pull.mul(side, side.dotproduct(to_axis) * force_side_k);
	float pull_mag = pull.magnitude();
	if (pull_mag > force_side_max) pull.mul(force_side_max / pull_mag);
	float into = norm.dotproduct(to_axis);
	dir.mul(axis, sign);
	dir.add(pull);
	dir.mad(norm, into > 0.f ? force_into : -force_into);
	dir.normalize();
}

extern bool g_actor_allow_ladder;

void CElevatorState::PhTune(float step)
{
	VERIFY(m_character&&m_character->b_exist&&m_character->is_active());
	m_step = step;
	if (!m_ladder) return;

	if ((!g_actor_allow_ladder && m_character->RestrictionType() == rtActor) || m_character->RestrictionType() == rtMonsterMedium)
	{
		if (m_state != clbNoLadder)
			UpdateDepart();
		else
		{
			m_state = clbNoLadder;
			m_ladder = NULL;
		}

		return;
	}

	switch (m_state)
	{
	case clbNone: UpdateStNone();
		break;
	case clbNearUp: UpdateStNearUp();
		break;
	case clbNearDown: UpdateStNearDown();
		break;
	case clbClimbingUp: UpdateStClimbingUp();
		break;
	case clbClimbingDown: UpdateStClimbingDown();
		break;
	case clbDepart: UpdateDepart();
		break;
	case clbNoLadder: m_ladder = NULL;
		break;
	}
}

void CElevatorState::PhDataUpdate(float step)
{
}

void CElevatorState::InitContact(dContact* c, bool& do_collide, u16, u16)
{
}

void CElevatorState::SetElevator(IClimableObject* climable)
{
	Fvector d;
	float dist = climable->DDToAxis(m_character, d);
	if (m_ladder == climable || dist > out_dist) return;
	if (m_ladder && m_ladder->DDToAxis(m_character, d) < dist) return;
	SwitchState(clbNone);
	m_ladder = climable;
}

void CElevatorState::SetCharacter(CPHCharacter* character)
{
	m_character = character;
	SwitchState(clbNoLadder);
}

void CElevatorState::EvaluateState()
{
	VERIFY(m_ladder&&m_character);
}

#ifdef DEBUG
const char*	dbg_state[] =	{
		"clbNone"			,				
		"clbNearUp"			,			
		"clbNearDown"		,		
		"clbClimbingUp"		,		
		"clbClimbingDown"	,	
		"clbDepart"			,
		"clbNoLadder"		
};
#endif
void CElevatorState::SwitchState(Estate new_state)
{
	if (!StateSwitchInertion(new_state))return;
#ifdef DEBUG
if(debug_output().ph_dbg_draw_mask().test(phDbgLadder))
				Msg("%s",dbg_state[new_state]);
#endif
	VERIFY(m_character);
	if ((m_state != clbClimbingUp && m_state != clbClimbingDown) &&
		(new_state == clbClimbingUp || new_state == clbClimbingDown)
	)
		dBodySetGravityMode(m_character->get_body(), 0);

	if ((new_state != clbClimbingUp && new_state != clbClimbingDown) &&
		(m_state == clbClimbingUp || m_state == clbClimbingDown)
	)
		dBodySetGravityMode(m_character->get_body(), 1);

	//if(new_state==clbDepart) InitDepart();
	NewState();
	m_state = new_state;
}

void CElevatorState::UpdateStNone()
{
	VERIFY(m_ladder&&m_character);
	Fvector d;
	m_ladder->DToPlain(m_character, d);
	if (m_ladder->BeforeLadder(m_character) &&
		m_ladder->InTouch(m_character) &&
		dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine)
	{
		if (ClimbDirection() > 0.f)
		{
			SwitchState(clbClimbingUp);
		}
		else
		{
			SwitchState(clbClimbingDown);
		}
	}
	else
	{
		Fvector temp;
		float d_to_lower = m_ladder->DDLowerP(m_character, temp), d_to_upper = m_ladder->DDUpperP(m_character, temp);
		if (d_to_lower < d_to_upper)
		{
			if (getting_on_dist + m_character->FootRadius() > d_to_lower)
				SwitchState(clbNearDown);
		}
		else
		{
			if (getting_on_dist + m_character->FootRadius() > d_to_upper)
				SwitchState(clbNearUp);
		}
	}
}

void CElevatorState::UpdateStNearUp()
{
	VERIFY(m_ladder&&m_character);
	Fvector d;

	if (m_ladder->InTouch(m_character) &&
		m_character->CamDir().y < -M_PI / 20.f &&
		//d.dotproduct(m_character->ControlAccel())<0.f&&
		//ClimbDirection()<0.f&&
		m_ladder->DDToPlain(m_character, d) > m_character->FootRadius() / 3.f &&
		m_ladder->BeforeLadder(m_character, 0.1f)
	)
		SwitchState(clbClimbingDown);
	float dist = m_ladder->DDUpperP(m_character, d);
	if (dist - m_character->FootRadius() > out_dist)SwitchState((clbNoLadder));
}

void CElevatorState::UpdateStNearDown()
{
	VERIFY(m_ladder&&m_character);
	Fvector d;
	float dist = m_ladder->DDLowerP(m_character, d);
	if (m_ladder->InTouch(m_character) &&
		dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine &&
		d.dotproduct(m_character->ControlAccel()) > 0.f &&
		ClimbDirection() > 0.f &&
		m_ladder->BeforeLadder(m_character)
	)
		SwitchState(clbClimbingUp);
	if (dist - m_character->FootRadius() > out_dist)SwitchState((clbNoLadder));
}


void CElevatorState::UpdateStClimbingDown()
{
	VERIFY(m_ladder&&m_character);
	Fvector d;

	if (ClimbDirection() > 0.f && m_ladder->BeforeLadder(m_character))
		SwitchState(clbClimbingUp);
	float to_ax = m_ladder->DDToAxis(m_character, d);
	Fvector ca;
	ca.set(m_character->ControlAccel());
	float control_a = to_mag_and_dir(ca);
	if (StrafeDepart(to_ax, ca, control_a))SwitchState(clbDepart);
	if (m_ladder->AxDistToLowerP(m_character) - m_character->FootRadius() < stop_climbing_dist)
		SwitchState(clbNearDown);
	UpdateClimbingCommon(d, to_ax, ca, control_a);

	if (m_ladder->AxDistToUpperP(m_character) < -m_character->FootRadius())SwitchState(clbNoLadder);

	Fvector vel;
	m_character->GetVelocity(vel);
	if (vel.y > EPS_S)
	{
		m_character->ApplyForce(0.f, -m_character->Mass() * ph_world->Gravity(), 0.f);
	}
	//if(to_ax-m_character->FootRadius()>out_dist)
	//														SwitchState((clbNone));
	//if(fis_zero(control_a)) 
	//	m_character->ApplyForce(d,m_character->Mass());
}

void CElevatorState::UpdateStClimbingUp()
{
	VERIFY(m_ladder&&m_character);
	Fvector d;

	if (m_exit)
	{
		UpdateExit();
		return;
	}
	if (ladder_actor(m_character) && ClimbDirection() > 0.f &&
		m_ladder->AxDistToUpperP(m_character) < exit_zone)
	{
		m_exit_probe -= m_step;
		if (m_exit_probe <= 0.f)
		{
			m_exit_probe = exit_probe_interval;
			if (FindExit())
			{
				m_exit = true;
				m_exit_leg = 0;
				m_exit_time = 0.f;
				m_exit_stall = 0.f;
				UpdateExit();
				return;
			}
		}
	}

	if (ClimbDirection() < 0.f && m_ladder->BeforeLadder(m_character))
		SwitchState(clbClimbingDown);
	float to_ax = m_ladder->DDToAxis(m_character, d);
	Fvector ca;
	ca.set(m_character->ControlAccel());
	float control_a = to_mag_and_dir(ca);
	if (StrafeDepart(to_ax, ca, control_a))SwitchState(clbDepart);
	if (m_ladder->AxDistToUpperP(m_character) + m_character->FootRadius() < stop_climbing_dist)
		SwitchState(clbNearUp);


	UpdateClimbingCommon(d, to_ax, ca, control_a);
	//if(to_ax-m_character->FootRadius()>out_dist)
	//										SwitchState((clbNone));
	//if(fis_zero(control_a)) 
	//	m_character->ApplyForce(d,m_character->Mass());
}

void CElevatorState::UpdateClimbingCommon(const Fvector& d_to_ax, float to_ax, const Fvector& control_accel, float ca)
{
	VERIFY(m_ladder&&m_character);
	if (to_ax - m_character->FootRadius() > out_dist)
		SwitchState((clbNoLadder));
	if (fis_zero(ca) && d_to_ax.dotproduct(m_ladder->Norm()) < 0.f && !ladder_actor(m_character))
	{
#ifdef DEBUG
		if(debug_output().ph_dbg_draw_mask().test(phDbgLadder))
		{
//.			Msg("force applied");
		}
#endif
		m_character->ApplyForce(d_to_ax, m_character->Mass() * ph_world->Gravity()); //
	}
}

bool CElevatorState::GetControlDir(Fvector& dir)
{
	bool ret = true;
	VERIFY(m_ladder&&m_character);
	Fvector d;
	float dist;
	switch (m_state)
	{
	case clbDepart:
	case clbNoLadder:
	case clbNone: break;
	case clbNearUp: dist = m_ladder->DDUpperP(m_character, d);
		if (dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine &&
			!fis_zero(dist, EPS_L) && m_character->ControlAccel().dotproduct(d) > 0.f)
			dir.set(d);
		break;
	case clbNearDown:
		dist = m_ladder->DDLowerP(m_character, d);
		if (dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine &&
			!fis_zero(dist, EPS_L) && m_character->ControlAccel().dotproduct(d) > 0.f)
			dir.set(d);
		break;
	case clbClimbingUp:
		if (ladder_actor(m_character))
		{
			ClimbForceDir(dir, 1.f);
			break;
		}
		m_ladder->DDAxis(dir);
		m_ladder->DDToAxis(m_character, d);
		dir.add(d);
		dir.normalize();
		break;
	case clbClimbingDown: m_ladder->DDToAxis(m_character, d);
		if (ladder_actor(m_character))
		{
			ClimbForceDir(dir, -1.f);
		}
		else if (m_ladder->BeforeLadder(m_character) || d.dotproduct(dir) > 0.f)
		{
			m_ladder->DDAxis(dir);
			dir.invert();
			dir.add(d);
			dir.normalize();
		}
		else
		{
#ifdef DEBUG
										if(debug_output().ph_dbg_draw_mask().test(phDbgLadder))
										{
											Msg("no c dir");
										}
#endif
			ret = false;
		}
		break;
	}
	return ret;
}

static const float depart_dist = 2.f;
static const u32 depart_time = 3000;

void CElevatorState::UpdateDepart()
{
	VERIFY(m_ladder&&m_character);
	Fvector temp;
	float d_to_lower = m_ladder->DDLowerP(m_character, temp), d_to_upper = m_ladder->DDUpperP(m_character, temp);
	if (d_to_lower < d_to_upper)
	{
		if (getting_on_dist + m_character->FootRadius() > d_to_lower)
			SwitchState(clbNearDown);
	}
	else
	{
		if (getting_on_dist + m_character->FootRadius() > d_to_upper)
			SwitchState(clbNearUp);
	}

	//Fvector p;m_character->GetFootCenter(p);
	//p.sub(m_start_position);
	//if(	p.magnitude()>depart_dist || 
	//	Device.dwTimeGlobal-m_start_time>depart_time)
	SwitchState(clbNoLadder);
}

void CElevatorState::NewState()
{
	VERIFY(m_character);
	m_start_time = inl_ph_world().Device().dwTimeGlobal;
	m_character->GetFootCenter(m_start_position);
	m_strafe_time = 0.f;
	m_exit = false; // any state change (a jump off, a depart) ends an exit
	m_exit_probe = 0.f;
}

void CElevatorState::Depart()
{
	VERIFY(m_character);
	if (m_ladder && ClimbingState())SwitchState(clbDepart);
}

void CElevatorState::GetLeaderNormal(Fvector& dir)
{
	if (!m_ladder) return;
	VERIFY(m_ladder&&m_character);
	m_ladder->DDNorm(dir);
	//Fvector d;
	//m_ladder->DToAxis(m_character,d);
	//if(dir.dotproduct(d)>0.f) dir.invert();
}

void CElevatorState::GetJumpDir(const Fvector& accel, Fvector& dir)
{
	VERIFY(m_ladder&&m_character);
	Fvector norm, side;
	m_ladder->DDNorm(norm);
	m_ladder->DDSide(side);
	Fvector ac;
	ac.set(accel).normalize_safe();
	float side_component = ac.dotproduct(side);
	dir.set(norm);
	if (_abs(side_component) > M_SQRT1_2)
	{
		if (side_component < 0.f)side.invert();
		dir.add(side);
		dir.normalize_safe();
	}
}

void CElevatorState::Deactivate()
{
	m_exit = false;
	SwitchState(clbNoLadder);
	m_state = clbNoLadder;
	m_ladder = NULL;
	m_character = NULL;
}


CElevatorState::SEnertionState CElevatorState::m_etable[clbNoState][clbNoState] =
{
	//						clbNone			clbNearUp		clbNearDown		clbClimbingUp	clbClimbingDown	clbDepart	clbNoLadder
	/*clbNone			*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, //clbNone			
	/*clbNearUp			*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, //clbNearUp		
	/*clbNearDown		*/ {{0, 0}, {0.0f, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, //clbNearDown		
	/*clbClimbingUp		*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, //clbClimbingUp	
	/*clbClimbingDown	*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, //clbClimbingDown	
	/*clbDepart			*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {depart_dist, depart_time}}, //clbDepart		
	/*clbNoLadder		*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}} //clbNoLadder		
};

bool CElevatorState::StateSwitchInertion(Estate new_state)
{
	Fvector p;
	m_character->GetFootCenter(p);
	p.sub(m_start_position);
	if (m_etable[m_state][new_state].dist < p.magnitude() || m_etable[m_state][new_state].time < inl_ph_world()
	                                                                                             .Device().dwTimeGlobal
		- m_start_time) return true;
	else return false;
}

bool CElevatorState::UpdateMaterial(u16& materil_idx)
{
	if (!ClimbingState())
		return false;
	VERIFY(m_ladder);
	materil_idx = m_ladder->Material();
	return true;
}

void CElevatorState::NetRelcase(IPhysicsShellHolder* O)
{
	if (!O || !m_ladder || O != m_ladder->cast_IPhysicsShellHolder())
		return;

	m_exit = false;
	m_state = clbNoLadder;
	m_ladder = NULL;
}
