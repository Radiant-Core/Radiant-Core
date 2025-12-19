// Copyright (c) 2026 The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <config.h>

/**
 * Start the Prometheus metrics HTTP handler.
 * Registers the /metrics endpoint.
 */
void StartPrometheusMetrics(Config& config);

/**
 * Stop the Prometheus metrics HTTP handler.
 * Unregisters the /metrics endpoint.
 */
void StopPrometheusMetrics();
