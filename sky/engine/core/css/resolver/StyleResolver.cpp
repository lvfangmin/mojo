/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2004-2005 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006, 2007 Nicholas Shanks (webkit@nickshanks.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007, 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "sky/engine/config.h"
#include "sky/engine/core/css/resolver/StyleResolver.h"

#include "gen/sky/core/CSSPropertyNames.h"
#include "gen/sky/core/MediaTypeNames.h"
#include "gen/sky/core/StylePropertyShorthand.h"
#include "gen/sky/platform/RuntimeEnabledFeatures.h"
#include "sky/engine/core/animation/ActiveAnimations.h"
#include "sky/engine/core/animation/Animation.h"
#include "sky/engine/core/animation/AnimationTimeline.h"
#include "sky/engine/core/animation/StyleInterpolation.h"
#include "sky/engine/core/animation/animatable/AnimatableValue.h"
#include "sky/engine/core/animation/css/CSSAnimatableValueFactory.h"
#include "sky/engine/core/animation/css/CSSAnimations.h"
#include "sky/engine/core/css/CSSCalculationValue.h"
#include "sky/engine/core/css/CSSFontSelector.h"
#include "sky/engine/core/css/CSSSelector.h"
#include "sky/engine/core/css/CSSValueList.h"
#include "sky/engine/core/css/CSSValuePool.h"
#include "sky/engine/core/css/ElementRuleCollector.h"
#include "sky/engine/core/css/FontFace.h"
#include "sky/engine/core/css/MediaQueryEvaluator.h"
#include "sky/engine/core/css/RuleSet.h"
#include "sky/engine/core/css/StylePropertySet.h"
#include "sky/engine/core/css/StyleSheetContents.h"
#include "sky/engine/core/css/parser/BisonCSSParser.h"
#include "sky/engine/core/css/resolver/AnimatedStyleBuilder.h"
#include "sky/engine/core/css/resolver/MatchResult.h"
#include "sky/engine/core/css/resolver/SharedStyleFinder.h"
#include "sky/engine/core/css/resolver/StyleAdjuster.h"
#include "sky/engine/core/css/resolver/StyleBuilder.h"
#include "sky/engine/core/css/resolver/StyleResolverState.h"
#include "sky/engine/core/css/resolver/StyleResolverStats.h"
#include "sky/engine/core/css/resolver/StyleResourceLoader.h"
#include "sky/engine/core/dom/NodeRenderStyle.h"
#include "sky/engine/core/dom/StyleEngine.h"
#include "sky/engine/core/dom/Text.h"
#include "sky/engine/core/dom/shadow/ElementShadow.h"
#include "sky/engine/core/dom/shadow/ShadowRoot.h"
#include "sky/engine/core/frame/FrameView.h"
#include "sky/engine/core/frame/LocalFrame.h"
#include "sky/engine/core/rendering/RenderView.h"
#include "sky/engine/wtf/LeakAnnotations.h"
#include "sky/engine/wtf/StdLibExtras.h"

namespace {

using namespace blink;

void setAnimationUpdateIfNeeded(StyleResolverState& state, Element& element)
{
    // If any changes to CSS Animations were detected, stash the update away for application after the
    // render object is updated if we're in the appropriate scope.
    if (state.animationUpdate())
        element.ensureActiveAnimations().cssAnimations().setPendingUpdate(state.takeAnimationUpdate());
}

} // namespace

namespace blink {

static RuleSet& defaultStyles()
{
    DEFINE_STATIC_LOCAL(RefPtr<StyleSheetContents>, styleSheet, ());
    DEFINE_STATIC_LOCAL(OwnPtr<RuleSet>, ruleSet, ());

    if (ruleSet)
        return *ruleSet;

    String cssText =
        "link, import, meta, script, style, template, title {\n"
        "    display: none;\n"
        "}\n"
        "t {\n"
        "   display: inline;"
        "}\n"
        "a {\n"
        "    color: blue;\n"
        "    display: inline;\n"
        "    text-decoration: underline;\n"
        "}\n";

    styleSheet = StyleSheetContents::create(0, CSSParserContext());
    styleSheet->parseString(cssText);

    ruleSet = RuleSet::create();
    ruleSet->addRulesFromSheet(styleSheet.get());

    return *ruleSet;
}

StyleResolver::StyleResolver(Document& document)
    : m_document(document)
    , m_styleResolverStatsSequence(0)
{
}

void StyleResolver::addToStyleSharingList(Element& element)
{
    // Never add elements to the style sharing list if we're not in a recalcStyle,
    // otherwise we could leave stale pointers in there.
    if (!m_document.inStyleRecalc())
        return;
    ASSERT(element.supportsStyleSharing());
    INCREMENT_STYLE_STATS_COUNTER(*this, sharedStyleCandidates);
    StyleSharingList& list = styleSharingList();
    if (list.size() >= styleSharingListSize)
        list.removeLast();
    list.prepend(&element);
}

void StyleResolver::clearStyleSharingList()
{
    m_styleSharingList.clear();
}

StyleResolver::~StyleResolver()
{
}

void StyleResolver::matchRules(Element& element, ElementRuleCollector& collector)
{
    collector.clearMatchedRules();

    CascadeOrder cascadeOrder = 0;

    collector.collectMatchingRules(MatchRequest(&defaultStyles()), ++cascadeOrder);

    if (ShadowRoot* shadowRoot = element.shadowRoot())
        shadowRoot->scopedStyleResolver().collectMatchingHostRules(collector, ++cascadeOrder);

    ScopedStyleResolver& resolver = element.treeScope().scopedStyleResolver();
    resolver.collectMatchingAuthorRules(collector, ++cascadeOrder);

    collector.sortAndTransferMatchedRules();

    if (const StylePropertySet* inlineStyle = element.inlineStyle()) {
        // Inline style is immutable as long as there is no CSSOM wrapper.
        bool isInlineStyleCacheable = !inlineStyle->isMutable();
        collector.addElementStyleProperties(inlineStyle, isInlineStyleCacheable);
    }
}

PassRefPtr<RenderStyle> StyleResolver::styleForDocument(Document& document)
{
    RefPtr<RenderStyle> documentStyle = RenderStyle::create();
    documentStyle->setDisplay(BLOCK);
    documentStyle->setRTLOrdering(LogicalOrder);
    documentStyle->setLocale(document.contentLanguage());
    documentStyle->setZIndex(0);
    documentStyle->setUserModify(READ_ONLY);

    document.setupFontBuilder(documentStyle.get());

    return documentStyle.release();
}

void StyleResolver::loadPendingResources(StyleResolverState& state)
{
    StyleResourceLoader loader(m_document.fetcher());
    loader.loadPendingResources(state.style(), state.elementStyleResources());
    m_document.styleEngine()->fontSelector()->fontLoader()->loadPendingFonts();
}

PassRefPtr<RenderStyle> StyleResolver::styleForElement(Element* element, RenderStyle* defaultParent)
{
    ASSERT(m_document.frame());
    ASSERT(m_document.settings());

    if (element == m_document.documentElement())
        m_document.setDirectionSetOnDocumentElement(false);
    StyleResolverState state(m_document, element, defaultParent);

    if (state.parentStyle()) {
        SharedStyleFinder styleFinder(state.elementContext(), *this);
        if (RefPtr<RenderStyle> sharedStyle = styleFinder.findSharedStyle())
            return sharedStyle.release();
    }

    if (state.parentStyle()) {
        state.setStyle(RenderStyle::create());
        state.style()->inheritFrom(state.parentStyle(), isAtShadowBoundary(element) ? RenderStyle::AtShadowBoundary : RenderStyle::NotAtShadowBoundary);
    } else {
        state.setStyle(defaultStyleForElement());
        state.setParentStyle(RenderStyle::clone(state.style()));
    }
    // contenteditable attribute (implemented by -webkit-user-modify) should
    // be propagated from shadow host to distributed node.
    if (state.distributedToInsertionPoint()) {
        if (Element* parent = element->parentElement()) {
            if (RenderStyle* styleOfShadowHost = parent->renderStyle())
                state.style()->setUserModify(styleOfShadowHost->userModify());
        }
    }

    state.fontBuilder().initForStyleResolve(state.document(), state.style());

    {
        ElementRuleCollector collector(state.elementContext(), state.style());

        matchRules(*element, collector);

        applyMatchedProperties(state, collector.matchedResult());
    }

    // Cache our original display.
    state.style()->setOriginalDisplay(state.style()->display());

    StyleAdjuster adjuster;
    adjuster.adjustRenderStyle(state.style(), state.parentStyle(), *element);

    if (applyAnimatedProperties(state, element))
        adjuster.adjustRenderStyle(state.style(), state.parentStyle(), *element);

    setAnimationUpdateIfNeeded(state, *element);

    if (state.style()->hasViewportUnits())
        m_document.setHasViewportUnits();

    // Now return the style.
    return state.takeStyle();
}

// This function is used by the WebAnimations JavaScript API method animate().
// FIXME: Remove this when animate() switches away from resolution-dependent parsing.
PassRefPtr<AnimatableValue> StyleResolver::createAnimatableValueSnapshot(Element& element, CSSPropertyID property, CSSValue& value)
{
    RefPtr<RenderStyle> style;
    if (element.renderStyle())
        style = RenderStyle::clone(element.renderStyle());
    else
        style = RenderStyle::create();
    StyleResolverState state(element.document(), &element);
    state.setStyle(style);
    state.fontBuilder().initForStyleResolve(state.document(), state.style());
    return createAnimatableValueSnapshot(state, property, value);
}

PassRefPtr<AnimatableValue> StyleResolver::createAnimatableValueSnapshot(StyleResolverState& state, CSSPropertyID property, CSSValue& value)
{
    StyleBuilder::applyProperty(property, state, &value);
    return CSSAnimatableValueFactory::create(property, *state.style());
}

PassRefPtr<RenderStyle> StyleResolver::defaultStyleForElement()
{
    StyleResolverState state(m_document, 0);
    state.setStyle(RenderStyle::create());
    state.fontBuilder().initForStyleResolve(m_document, state.style());
    state.style()->setLineHeight(RenderStyle::initialLineHeight());
    state.setLineHeightValue(0);
    state.fontBuilder().setInitial();
    state.style()->font().update(m_document.styleEngine()->fontSelector());
    return state.takeStyle();
}

PassRefPtr<RenderStyle> StyleResolver::styleForText(Text* textNode)
{
    ASSERT(textNode);

    Node* parentNode = NodeRenderingTraversal::parent(textNode);
    if (!parentNode || !parentNode->renderStyle())
        return defaultStyleForElement();
    return parentNode->renderStyle();
}

void StyleResolver::updateFont(StyleResolverState& state)
{
    state.fontBuilder().createFont(m_document.styleEngine()->fontSelector(), state.parentStyle(), state.style());
    if (state.fontBuilder().fontSizeHasViewportUnits())
        state.style()->setHasViewportUnits();
}

// -------------------------------------------------------------------------------------
// this is mostly boring stuff on how to apply a certain rule to the renderstyle...

bool StyleResolver::applyAnimatedProperties(StyleResolverState& state, Element* animatingElement)
{
    const Element* element = state.element();
    ASSERT(element);

    // The animating element may be this element, or its pseudo element. It is
    // null when calculating the style for a potential pseudo element that has
    // yet to be created.
    ASSERT(animatingElement == element || !animatingElement || animatingElement->parentOrShadowHostElement() == element);

    if (!(animatingElement && animatingElement->hasActiveAnimations())
        && !state.style()->transitions() && !state.style()->animations())
        return false;

    state.setAnimationUpdate(CSSAnimations::calculateUpdate(animatingElement, *element, *state.style(), state.parentStyle()));
    if (!state.animationUpdate())
        return false;

    const HashMap<CSSPropertyID, RefPtr<Interpolation> >& activeInterpolationsForAnimations = state.animationUpdate()->activeInterpolationsForAnimations();
    const HashMap<CSSPropertyID, RefPtr<Interpolation> >& activeInterpolationsForTransitions = state.animationUpdate()->activeInterpolationsForTransitions();
    applyAnimatedProperties<HighPriorityProperties>(state, activeInterpolationsForAnimations);
    applyAnimatedProperties<HighPriorityProperties>(state, activeInterpolationsForTransitions);

    updateFont(state);

    applyAnimatedProperties<LowPriorityProperties>(state, activeInterpolationsForAnimations);
    applyAnimatedProperties<LowPriorityProperties>(state, activeInterpolationsForTransitions);

    // Start loading resources used by animations.
    loadPendingResources(state);

    ASSERT(!state.fontBuilder().fontDirty());

    return true;
}

template <StyleResolver::StyleApplicationPass pass>
void StyleResolver::applyAnimatedProperties(StyleResolverState& state, const HashMap<CSSPropertyID, RefPtr<Interpolation> >& activeInterpolations)
{
    for (HashMap<CSSPropertyID, RefPtr<Interpolation> >::const_iterator iter = activeInterpolations.begin(); iter != activeInterpolations.end(); ++iter) {
        CSSPropertyID property = iter->key;
        if (!isPropertyForPass<pass>(property))
            continue;
        const StyleInterpolation* interpolation = toStyleInterpolation(iter->value.get());
        interpolation->apply(state);
    }
}

// FIXME: Consider refactoring to create a new class which owns the following
// first/last/range properties.
// This method returns the first CSSPropertyId of high priority properties.
// Other properties can depend on high priority properties. For example,
// border-color property with currentColor value depends on color property.
// All high priority properties are obtained by using
// firstCSSPropertyId<HighPriorityProperties> and
// lastCSSPropertyId<HighPriorityProperties>.
template<> CSSPropertyID StyleResolver::firstCSSPropertyId<StyleResolver::HighPriorityProperties>()
{
    COMPILE_ASSERT(CSSPropertyColor == firstCSSProperty, CSS_color_is_first_high_priority_property);
    return CSSPropertyColor;
}

// This method returns the last CSSPropertyId of high priority properties.
template<> CSSPropertyID StyleResolver::lastCSSPropertyId<StyleResolver::HighPriorityProperties>()
{
    COMPILE_ASSERT(CSSPropertyLineHeight == CSSPropertyColor + 16, CSS_line_height_is_end_of_high_prioity_property_range);
    COMPILE_ASSERT(CSSPropertyTextRendering == CSSPropertyLineHeight - 1, CSS_text_rendering_is_before_line_height);
    return CSSPropertyLineHeight;
}

// This method returns the first CSSPropertyId of remaining properties,
// i.e. low priority properties. No properties depend on low priority
// properties. So we don't need to resolve such properties quickly.
// All low priority properties are obtained by using
// firstCSSPropertyId<LowPriorityProperties> and
// lastCSSPropertyId<LowPriorityProperties>.
template<> CSSPropertyID StyleResolver::firstCSSPropertyId<StyleResolver::LowPriorityProperties>()
{
    COMPILE_ASSERT(CSSPropertyAlignContent == CSSPropertyLineHeight + 1, CSS_background_is_first_low_priority_property);
    return CSSPropertyAlignContent;
}

// This method returns the last CSSPropertyId of low priority properties.
template<> CSSPropertyID StyleResolver::lastCSSPropertyId<StyleResolver::LowPriorityProperties>()
{
    return static_cast<CSSPropertyID>(lastCSSProperty);
}

template <StyleResolver::StyleApplicationPass pass>
bool StyleResolver::isPropertyForPass(CSSPropertyID property)
{
    return firstCSSPropertyId<pass>() <= property && property <= lastCSSPropertyId<pass>();
}

// This method expands the 'all' shorthand property to longhand properties
// and applies the expanded longhand properties.
template <StyleResolver::StyleApplicationPass pass>
void StyleResolver::applyAllProperty(StyleResolverState& state, CSSValue* allValue)
{
    bool isUnsetValue = !allValue->isInitialValue() && !allValue->isInheritedValue();
    unsigned startCSSProperty = firstCSSPropertyId<pass>();
    unsigned endCSSProperty = lastCSSPropertyId<pass>();

    for (unsigned i = startCSSProperty; i <= endCSSProperty; ++i) {
        CSSPropertyID propertyId = static_cast<CSSPropertyID>(i);

        // StyleBuilder does not allow any expanded shorthands.
        if (isExpandedShorthandForAll(propertyId))
            continue;

        // all shorthand spec says:
        // The all property is a shorthand that resets all CSS properties
        // except direction and unicode-bidi.
        // c.f. http://dev.w3.org/csswg/css-cascade/#all-shorthand
        // We skip applyProperty when a given property is unicode-bidi or
        // direction.
        if (!CSSProperty::isAffectedByAllProperty(propertyId))
            continue;

        CSSValue* value;
        if (!isUnsetValue) {
            value = allValue;
        } else {
            if (CSSPropertyMetadata::isInheritedProperty(propertyId))
                value = cssValuePool().createInheritedValue().get();
            else
                value = cssValuePool().createExplicitInitialValue().get();
        }
        StyleBuilder::applyProperty(propertyId, state, value);
    }
}

template <StyleResolver::StyleApplicationPass pass>
void StyleResolver::applyProperties(StyleResolverState& state, const StylePropertySet* properties, bool inheritedOnly)
{
    unsigned propertyCount = properties->propertyCount();
    for (unsigned i = 0; i < propertyCount; ++i) {
        StylePropertySet::PropertyReference current = properties->propertyAt(i);

        CSSPropertyID property = current.id();
        if (property == CSSPropertyAll) {
            applyAllProperty<pass>(state, current.value());
            continue;
        }

        if (inheritedOnly && !current.isInherited()) {
            // If the property value is explicitly inherited, we need to apply further non-inherited properties
            // as they might override the value inherited here. For this reason we don't allow declarations with
            // explicitly inherited properties to be cached.
            ASSERT(!current.value()->isInheritedValue());
            continue;
        }

        if (!isPropertyForPass<pass>(property))
            continue;
        if (pass == HighPriorityProperties && property == CSSPropertyLineHeight)
            state.setLineHeightValue(current.value());
        else
            StyleBuilder::applyProperty(current.id(), state, current.value());
    }
}

template <StyleResolver::StyleApplicationPass pass>
void StyleResolver::applyMatchedProperties(StyleResolverState& state, const MatchResult& matchResult, bool inheritedOnly)
{
    for (const RefPtr<StylePropertySet>& properties : matchResult.matchedProperties) {
        applyProperties<pass>(state, properties.get(), inheritedOnly);
    }
}

static unsigned computeMatchedPropertiesHash(const RefPtr<StylePropertySet>* properties, unsigned size)
{
    return StringHasher::hashMemory(properties, sizeof(*properties) * size);
}

void StyleResolver::invalidateMatchedPropertiesCache()
{
    m_matchedPropertiesCache.clear();
}

void StyleResolver::notifyResizeForViewportUnits()
{
    m_matchedPropertiesCache.clearViewportDependent();
}

void StyleResolver::applyMatchedProperties(StyleResolverState& state, const MatchResult& matchResult)
{
    const Element* element = state.element();
    ASSERT(element);

    INCREMENT_STYLE_STATS_COUNTER(*this, matchedPropertyApply);

    unsigned cacheHash = matchResult.isCacheable ? computeMatchedPropertiesHash(matchResult.matchedProperties.data(), matchResult.matchedProperties.size()) : 0;
    bool applyInheritedOnly = false;
    const CachedMatchedProperties* cachedMatchedProperties = cacheHash ? m_matchedPropertiesCache.find(cacheHash, state, matchResult) : 0;

    if (cachedMatchedProperties && MatchedPropertiesCache::isCacheable(element, state.style(), state.parentStyle())) {
        INCREMENT_STYLE_STATS_COUNTER(*this, matchedPropertyCacheHit);
        // We can build up the style by copying non-inherited properties from an earlier style object built using the same exact
        // style declarations. We then only need to apply the inherited properties, if any, as their values can depend on the
        // element context. This is fast and saves memory by reusing the style data structures.
        state.style()->copyNonInheritedFrom(cachedMatchedProperties->renderStyle.get());
        if (state.parentStyle()->inheritedDataShared(cachedMatchedProperties->parentRenderStyle.get()) && !isAtShadowBoundary(element)
            && (!state.distributedToInsertionPoint() || state.style()->userModify() == READ_ONLY)) {
            INCREMENT_STYLE_STATS_COUNTER(*this, matchedPropertyCacheInheritedHit);

            // If the cache item parent style has identical inherited properties to the current parent style then the
            // resulting style will be identical too. We copy the inherited properties over from the cache and are done.
            state.style()->inheritFrom(cachedMatchedProperties->renderStyle.get());

            return;
        }
        applyInheritedOnly = true;
    }

    state.setLineHeightValue(0);
    applyMatchedProperties<HighPriorityProperties>(state, matchResult, applyInheritedOnly);

    // If our font got dirtied, go ahead and update it now.
    updateFont(state);

    // Line-height is set when we are sure we decided on the font-size.
    if (state.lineHeightValue())
        StyleBuilder::applyProperty(CSSPropertyLineHeight, state, state.lineHeightValue());

    // Many properties depend on the font. If it changes we just apply all properties.
    if (cachedMatchedProperties && cachedMatchedProperties->renderStyle->fontDescription() != state.style()->fontDescription())
        applyInheritedOnly = false;

    applyMatchedProperties<LowPriorityProperties>(state, matchResult, applyInheritedOnly);

    loadPendingResources(state);

    if (!cachedMatchedProperties && cacheHash && MatchedPropertiesCache::isCacheable(element, state.style(), state.parentStyle())) {
        INCREMENT_STYLE_STATS_COUNTER(*this, matchedPropertyCacheAdded);
        m_matchedPropertiesCache.add(state.style(), state.parentStyle(), cacheHash, matchResult);
    }

    ASSERT(!state.fontBuilder().fontDirty());
}

CSSPropertyValue::CSSPropertyValue(CSSPropertyID id, const StylePropertySet& propertySet)
    : property(id), value(propertySet.getPropertyCSSValue(id).get())
{ }

void StyleResolver::enableStats(StatsReportType reportType)
{
    if (m_styleResolverStats)
        return;
    m_styleResolverStats = StyleResolverStats::create();
    m_styleResolverStatsTotals = StyleResolverStats::create();
    if (reportType == ReportSlowStats) {
        m_styleResolverStats->printMissedCandidateCount = true;
        m_styleResolverStatsTotals->printMissedCandidateCount = true;
    }
}

void StyleResolver::disableStats()
{
    m_styleResolverStatsSequence = 0;
    m_styleResolverStats.clear();
    m_styleResolverStatsTotals.clear();
}

void StyleResolver::printStats()
{
    if (!m_styleResolverStats)
        return;
    fprintf(stderr, "=== Style Resolver Stats (resolve #%u) (%s) ===\n", ++m_styleResolverStatsSequence, m_document.url().string().utf8().data());
    fprintf(stderr, "%s\n", m_styleResolverStats->report().utf8().data());
    fprintf(stderr, "== Totals ==\n");
    fprintf(stderr, "%s\n", m_styleResolverStatsTotals->report().utf8().data());
}

void StyleResolver::applyPropertiesToStyle(const CSSPropertyValue* properties, size_t count, RenderStyle* style)
{
    StyleResolverState state(m_document, m_document.documentElement(), style);
    state.setStyle(style);

    state.fontBuilder().initForStyleResolve(m_document, style);

    for (size_t i = 0; i < count; ++i) {
        if (properties[i].value) {
            // As described in BUG66291, setting font-size and line-height on a font may entail a CSSPrimitiveValue::computeLengthDouble call,
            // which assumes the fontMetrics are available for the affected font, otherwise a crash occurs (see http://trac.webkit.org/changeset/96122).
            // The updateFont() call below updates the fontMetrics and ensure the proper setting of font-size and line-height.
            switch (properties[i].property) {
            case CSSPropertyFontSize:
            case CSSPropertyLineHeight:
                updateFont(state);
                break;
            default:
                break;
            }
            StyleBuilder::applyProperty(properties[i].property, state, properties[i].value);
        }
    }
}

} // namespace blink
