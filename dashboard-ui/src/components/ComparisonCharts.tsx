import React from 'react';
import {
  LineChart,
  Line,
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from 'recharts';
import { Summary, CompareSummary, MetricsEvent } from '../types/metrics';

interface ComparisonChartsProps {
  summary: Summary | CompareSummary | null;
  events: MetricsEvent[];
  mode: 'single' | 'compare';
}

export function ComparisonCharts({ summary, events, mode }: ComparisonChartsProps) {
  if (!summary || events.length === 0) return null;

  const isCompare = mode === 'compare';
  const compareSummary = summary as CompareSummary;
  const singleSummary = 'overall' in summary ? compareSummary.overall! : (summary as Summary);

  const latencyByIndex = events.map((e, i) => ({
    index: i,
    latency: e.latency_us,
    source: e.source,
    filter: e.filter_type,
  }));

  const bloomSummary = compareSummary.bloom;
  const surfSummary = compareSummary.surf;
  const latencyDifference = bloomSummary && surfSummary
    ? surfSummary.avg_latency_us - bloomSummary.avg_latency_us
    : null;

  const latencyByFilterType = isCompare ? [
    { filter_type: 'bloom', bloom: bloomSummary?.avg_latency_us ?? 0, surf: surfSummary?.avg_latency_us ?? 0 },
  ] : Object.entries(singleSummary.avg_latency_by_filter_type || {}).map(([ft, lat]) => ({
    filter_type: ft,
    latency: lat,
  }));

  const eventsByQueryType = isCompare ? [
    { query_type: 'point_get', bloom: bloomSummary?.count_by_query_type?.['point_get'] || 0, surf: surfSummary?.count_by_query_type?.['point_get'] || 0 },
    { query_type: 'range_scan', bloom: bloomSummary?.count_by_query_type?.['range_scan'] || 0, surf: surfSummary?.count_by_query_type?.['range_scan'] || 0 },
  ] : Object.entries(singleSummary.count_by_query_type || {}).map(([qt, count]) => ({
    query_type: qt,
    count,
  }));

  const eventsByBenchmark = isCompare ? [
    { benchmark_name: 'fillrandom', bloom: bloomSummary?.count_by_benchmark_name?.['fillrandom'] || 0, surf: surfSummary?.count_by_benchmark_name?.['fillrandom'] || 0 },
    { benchmark_name: 'readrandom', bloom: bloomSummary?.count_by_benchmark_name?.['readrandom'] || 0, surf: surfSummary?.count_by_benchmark_name?.['readrandom'] || 0 },
    { benchmark_name: 'readseq', bloom: bloomSummary?.count_by_benchmark_name?.['readseq'] || 0, surf: surfSummary?.count_by_benchmark_name?.['readseq'] || 0 },
    { benchmark_name: 'seekrandom', bloom: bloomSummary?.count_by_benchmark_name?.['seekrandom'] || 0, surf: surfSummary?.count_by_benchmark_name?.['seekrandom'] || 0 },
  ] : Object.entries(singleSummary.count_by_benchmark_name || {}).map(([bn, count]) => ({
    benchmark_name: bn,
    count,
  }));

  const pruneData = isCompare ? [
    {
      type: 'Considered',
      bloom: bloomSummary?.total_considered ?? 0,
      surf: surfSummary?.total_considered ?? 0,
    },
    {
      type: 'Pruned',
      bloom: bloomSummary?.total_pruned ?? 0,
      surf: surfSummary?.total_pruned ?? 0,
    },
    {
      type: 'Opened',
      bloom: bloomSummary?.total_opened ?? 0,
      surf: surfSummary?.total_opened ?? 0,
    },
  ] : [
    {
      type: 'Considered',
      count: singleSummary.total_considered,
    },
    {
      type: 'Pruned',
      count: singleSummary.total_pruned,
    },
    {
      type: 'Opened',
      count: singleSummary.total_opened,
    },
  ];

  return (
    <div className="max-w-7xl mx-auto px-6 py-8">
      <h2 className="text-2xl font-bold text-slate-900 mb-6">Analytics</h2>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8 mb-8">
        {/* Latency over time */}
        <ChartCard title="Latency Over Event Index">
          <ResponsiveContainer width="100%" height={300}>
            <LineChart data={latencyByIndex.slice(-500)}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e2e8f0" />
              <XAxis dataKey="index" stroke="#94a3b8" />
              <YAxis stroke="#94a3b8" />
              <Tooltip
                contentStyle={{
                  backgroundColor: '#f8fafc',
                  border: '1px solid #e2e8f0',
                }}
              />
              <Legend />
              {isCompare ? (
                <>
                  <Line
                    dataKey="latency"
                    stroke="#3b82f6"
                    dot={false}
                    isAnimationActive={false}
                    name="Bloom"
                    data={latencyByIndex.filter(e => e.source === 'bloom').slice(-500)}
                  />
                  <Line
                    dataKey="latency"
                    stroke="#8b5cf6"
                    dot={false}
                    isAnimationActive={false}
                    name="SuRF"
                    data={latencyByIndex.filter(e => e.source === 'surf').slice(-500)}
                  />
                </>
              ) : (
                <Line
                  dataKey="latency"
                  stroke="#3b82f6"
                  dot={false}
                  isAnimationActive={false}
                />
              )}
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        {/* Latency by filter type */}
        <ChartCard title="Avg Latency by Filter Type">
          <ResponsiveContainer width="100%" height={300}>
            <BarChart data={latencyByFilterType}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e2e8f0" />
              <XAxis dataKey="filter_type" stroke="#94a3b8" />
              <YAxis stroke="#94a3b8" />
              <Tooltip
                contentStyle={{
                  backgroundColor: '#f8fafc',
                  border: '1px solid #e2e8f0',
                }}
              />
              {isCompare ? (
                <>
                  <Bar dataKey="bloom" fill="#3b82f6" name="Bloom" />
                  <Bar dataKey="surf" fill="#8b5cf6" name="SuRF" />
                </>
              ) : (
                <Bar dataKey="latency" fill="#3b82f6" />
              )}
            </BarChart>
          </ResponsiveContainer>
        </ChartCard>
      </div>

      {isCompare && (
        <div className="max-w-7xl mx-auto px-6 mb-8">
          <ChartCard title="Latency Difference (SuRF - Bloom)">
            <div className="text-center py-8">
              <p className="text-3xl font-semibold text-slate-900">
                {latencyDifference === null ? 'Source not loaded' : `${latencyDifference >= 0 ? '+' : ''}${latencyDifference} µs`}
              </p>
              <p className="text-sm text-slate-600 mt-2">
                {latencyDifference === null
                  ? 'Comparison requires both Bloom and SuRF data.'
                  : latencyDifference >= 0
                  ? 'SuRF is slower on average.'
                  : 'SuRF is faster on average.'}
              </p>
            </div>
          </ChartCard>
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8 mb-8">
        {/* Events by query type */}
        <ChartCard title="Event Count by Query Type">
          <ResponsiveContainer width="100%" height={300}>
            <BarChart data={eventsByQueryType}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e2e8f0" />
              <XAxis dataKey="query_type" stroke="#94a3b8" angle={-45} textAnchor="end" height={80} />
              <YAxis stroke="#94a3b8" />
              <Tooltip
                contentStyle={{
                  backgroundColor: '#f8fafc',
                  border: '1px solid #e2e8f0',
                }}
              />
              {isCompare ? (
                <>
                  <Bar dataKey="bloom" fill="#3b82f6" name="Bloom" />
                  <Bar dataKey="surf" fill="#8b5cf6" name="SuRF" />
                </>
              ) : (
                <Bar dataKey="count" fill="#8b5cf6" />
              )}
            </BarChart>
          </ResponsiveContainer>
        </ChartCard>

        {/* Pruned vs Opened */}
        <ChartCard title="SSTable Filter Results">
          <ResponsiveContainer width="100%" height={300}>
            <BarChart data={pruneData}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e2e8f0" />
              <XAxis dataKey="type" stroke="#94a3b8" />
              <YAxis stroke="#94a3b8" />
              <Tooltip
                contentStyle={{
                  backgroundColor: '#f8fafc',
                  border: '1px solid #e2e8f0',
                }}
              />
              {isCompare ? (
                <>
                  <Bar dataKey="bloom" fill="#3b82f6" name="Bloom" />
                  <Bar dataKey="surf" fill="#8b5cf6" name="SuRF" />
                </>
              ) : (
                <Bar dataKey="count" fill="#06b6d4" />
              )}
            </BarChart>
          </ResponsiveContainer>
        </ChartCard>
      </div>

      <div className="grid grid-cols-1 gap-8">
        {/* Events by benchmark */}
        <ChartCard title="Events by Benchmark">
          <ResponsiveContainer width="100%" height={300}>
            <BarChart data={eventsByBenchmark}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e2e8f0" />
              <XAxis dataKey="benchmark_name" stroke="#94a3b8" angle={-45} textAnchor="end" height={80} />
              <YAxis stroke="#94a3b8" />
              <Tooltip
                contentStyle={{
                  backgroundColor: '#f8fafc',
                  border: '1px solid #e2e8f0',
                }}
              />
              {isCompare ? (
                <>
                  <Bar dataKey="bloom" fill="#3b82f6" name="Bloom" />
                  <Bar dataKey="surf" fill="#8b5cf6" name="SuRF" />
                </>
              ) : (
                <Bar dataKey="count" fill="#10b981" />
              )}
            </BarChart>
          </ResponsiveContainer>
        </ChartCard>
      </div>
    </div>
  );
}

function ChartCard({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="card p-6">
      <h3 className="text-lg font-semibold text-slate-900 mb-4">{title}</h3>
      {children}
    </div>
  );
}
