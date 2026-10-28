#include "Plugin.h"

void AntiWallHack::FillSamples( const Snapshot& target, float padding, std::array<Vector, 12>& points )
{
    auto lo = target.mins - Vector( padding, padding, 0 );
    auto hi = target.maxs + Vector( padding, padding, padding );
    points[0] = target.eye;
    points[1] = target.origin + ( lo + hi ) * .5f;

    for ( int i = 0; i < 8; ++i )
    {
        points[i + 2] = target.origin + Vector( ( i & 1 ) ? hi.x : lo.x, ( i & 2 ) ? hi.y : lo.y, ( i & 4 ) ? hi.z : lo.z + 4 );
    }

    constexpr float radians = 3.14159265358979323846f / 180;
    float pitch = target.angles.x * radians, yaw = target.angles.y * radians;
    Vector forward( std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), -std::sin( pitch ) );
    points[10] = target.eye + forward * 64;
    points[11] = target.origin + Vector( 0, 0, 32 ) + forward * 40;
}

void AntiWallHack::BuildSamples( Snapshot& target )
{
    FillSamples( target, settings.BoundsPadding, target.samples );
    FillSamples( target, 28, target.smokeSamples );
    target.samplesReady = true;
}

void AntiWallHack::PrepareViewerPrediction( Snapshot& viewer )
{
    if ( viewer.eyeLeadReady )
    {
        return;
    }

    viewer.eyeLeadReady = true;
    viewer.visPeekCell = viewer.visCell;

    if ( !settings.CornerPrediction )
    {
        return;
    }

    const Vector wanted = CornerLead( viewer.velocity, viewer.angles.y, viewer.movementButtons, settings.CornerPredictionSeconds, settings.CornerMaxDistance );
    const float distance = wanted.Length();

    if ( distance > .01f && Finite( wanted ) && traceManager && traceFunction && traces < TraceBudget )
    {
        SightFilter filter( viewer, viewer );
        Ray_t ray;
        alignas( 16 ) CGameTrace result;
        Vector start = viewer.eye, end = start + wanted;
        ++traces;

        traceFunction( traceManager, ray, start, end, &filter, &result );

        viewer.eyeLead = wanted * CornerClipScale( distance, result.m_flFraction, result.m_bStartInSolid );
    }

    // Unknown/nonstandard VIS cells stay unknown. Apply the same clipped lead
    // to both the VIS lookup and later wall/smoke sample origins.
    if ( viewer.visCell >= 0 )
    {
        viewer.visPeekCell = visibility.Cell( viewer.origin + viewer.eyeLead );
    }
}

DirectVisibility AntiWallHack::CheckVisibility( int viewerSlot, int targetSlot )
{
    auto& viewer = players[viewerSlot];
    auto& target = players[targetSlot];

    PrepareViewerPrediction( viewer );
    auto eyeLead = viewer.eyeLead;
    // The baker's dilation covers default wall padding. Larger custom bounds use engine rays.
    if ( visibility.BlockedCells( viewer.visCell, viewer.visPeekCell, target.visCell, target.visCell ) )
    {
        return DirectVisibility::Hidden;
    }

    if ( !target.samplesReady )
    {
        BuildSamples( target );
    }

    int count = eyeLead.LengthSqr() < 1 ? 12 : 24;
    int first = preferred[viewerSlot][targetSlot];

    if ( first >= count )
    {
        first %= 12;
    }

    // Verified smoke can hide independently of uncertain or exhausted wall rays.
    if ( IsSmokeHidden( viewer, target, eyeLead, count, first ) )
    {
        return DirectVisibility::Hidden;
    }

    if ( !traceManager || !traceFunction )
    {
        return DirectVisibility::Unknown;
    }

    SightFilter filter( viewer, target );

    for ( int order = 0; order < count; ++order )
    {
        if ( traces >= TraceBudget )
        {
            return DirectVisibility::Unknown;
        }

        int index = OrderedSample( order, first );
        bool prediction = index >= 12;

        Vector start = viewer.eye + ( prediction ? eyeLead : Vector( 0, 0, 0 ) );
        Vector end = target.samples[index % 12];

        if ( !Finite( start ) || !Finite( end ) )
        {
            return DirectVisibility::Unknown;
        }

        Ray_t ray;
        alignas( 16 ) CGameTrace result;
        ++traces;

        traceFunction( traceManager, ray, start, end, &filter, &result );

        if ( !Occluded( result.m_flFraction, result.m_bStartInSolid ) )
        {
            bool clear = !result.m_bStartInSolid && std::isfinite( result.m_flFraction ) && result.m_flFraction >= .99f && result.m_flFraction <= 1;

            if ( !clear )
            {
                return DirectVisibility::Unknown;
            }

            preferred[viewerSlot][targetSlot] = index;
            return DirectVisibility::Visible;
        }

        if ( result.m_pEnt && !filter.ShouldHitEntity( result.m_pEnt ) )
        {
            return DirectVisibility::Unknown;
        }
    }

    return DirectVisibility::Hidden;
}

KHook::Return<bool> AntiWallHack::OnTrace( void* manager, Ray_t&, Vector&, Vector&, CTraceFilter*, CGameTrace* )
{
    if ( settings.Debug && !debugTraceSeen.exchange( true ) )
    {
        DebugStage( "OnTrace: first engine trace callback" );
    }

    traceManager = manager;
    return { KHook::Action::Ignore };
}
