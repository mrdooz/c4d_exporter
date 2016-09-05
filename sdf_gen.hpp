#pragma once
#include "exporter_types.hpp"

// clang-format off
enum TriangleFeature
{
  FeatureVertex   = 0x8,
  FeatureEdge     = 0x10,
  FeatureFace     = 0x20,

  FeatureVertexA  = FeatureVertex + 0,
  FeatureVertexB  = FeatureVertex + 1,
  FeatureVertexC  = FeatureVertex + 2,
  FeatureEdgeAB   = FeatureEdge + 0,
  FeatureEdgeAC   = FeatureEdge + 1,
  FeatureEdgeBC   = FeatureEdge + 2,
};
// clang-format on

vec3 ClosestPtvec3Triangle(const vec3& p, const vec3& a, const vec3& b, const vec3& c, TriangleFeature* feature);
