import React, { useState } from 'react';
import { MetricsEvent } from '../types/metrics';
import { formatLatency, getFilterColor, getQueryTypeColor } from '../lib/metrics';
import { ChevronDown } from 'lucide-react';

interface EventTableProps {
  events: MetricsEvent[];
  onSelectEvent?: (event: MetricsEvent) => void;
}

export function EventTable({ events, onSelectEvent }: EventTableProps) {
  const [expandedId, setExpandedId] = useState<number | null>(null);

  if (events.length === 0) {
    return (
      <div className="max-w-7xl mx-auto px-6 py-8">
        <div className="card p-8 text-center">
          <p className="text-slate-600">No events to display</p>
        </div>
      </div>
    );
  }

  return (
    <div className="max-w-7xl mx-auto px-6 py-8">
      <h2 className="text-2xl font-bold text-slate-900 mb-4">Query Trace</h2>
      <div className="card overflow-x-auto">
        <table className="w-full text-sm">
          <thead className="bg-slate-50 border-b border-slate-200">
            <tr>
              <th className="px-4 py-3 text-left font-semibold text-slate-700">ID</th>
              <th className="px-4 py-3 text-left font-semibold text-slate-700">Benchmark</th>
              <th className="px-4 py-3 text-left font-semibold text-slate-700">Filter</th>
              <th className="px-4 py-3 text-left font-semibold text-slate-700">Query Type</th>
              <th className="px-4 py-3 text-left font-semibold text-slate-700">Latency</th>
              <th className="px-4 py-3 text-left font-semibold text-slate-700">Match</th>
              <th className="px-4 py-3 text-left font-semibold text-slate-700">Details</th>
            </tr>
          </thead>
          <tbody>
            {events.map((event, i) => {
              const isExpanded = expandedId === event.query_id;
              return (
                <React.Fragment key={event.query_id}>
                  <tr
                    className="border-b border-slate-200 hover:bg-slate-50"
                    onClick={() => onSelectEvent?.(event)}
                  >
                    <td className="px-4 py-2">{event.query_id}</td>
                    <td className="px-4 py-2 text-slate-700">{event.benchmark_name}</td>
                    <td className="px-4 py-2">
                      <Badge text={event.filter_type} className={getFilterColor(event.filter_type)} />
                    </td>
                    <td className="px-4 py-2">
                      <Badge text={event.query_type} className={getQueryTypeColor(event.query_type)} />
                    </td>
                    <td className="px-4 py-2 font-mono">{formatLatency(event.latency_us)}</td>
                    <td className="px-4 py-2">
                      <Badge
                        text={event.actual_match ? 'Hit' : 'Miss'}
                        className={event.actual_match ? 'badge-success' : 'badge-danger'}
                      />
                    </td>
                    <td className="px-4 py-2">
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          setExpandedId(isExpanded ? null : event.query_id);
                        }}
                        className="p-1 hover:bg-slate-200 rounded"
                      >
                        <ChevronDown
                          className={`w-4 h-4 transition-transform ${
                            isExpanded ? 'transform rotate-180' : ''
                          }`}
                        />
                      </button>
                    </td>
                  </tr>
                  {isExpanded && (
                    <tr className="bg-slate-50 border-b border-slate-200">
                      <td colSpan={7} className="px-4 py-3">
                        <EventDetails event={event} />
                      </td>
                    </tr>
                  )}
                </React.Fragment>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}

function Badge({ text, className }: { text: string; className: string }) {
  return <span className={`badge ${className}`}>{text}</span>;
}

function EventDetails({ event }: { event: MetricsEvent }) {
  return (
    <div className="grid grid-cols-2 md:grid-cols-4 gap-4 text-sm">
      <Detail label="Filter May Match" value={event.filter_may_match !== undefined ? (event.filter_may_match ? 'Yes' : 'No') : '—'} />
      <Detail label="False Positive" value={event.false_positive !== undefined ? (event.false_positive ? 'Yes' : 'No') : '—'} />
      <Detail label="SSTables Considered" value={event.sstables_considered?.toString() || '—'} />
      <Detail label="SSTables Pruned" value={event.sstables_pruned?.toString() || '—'} />
      <Detail label="SSTables Opened" value={event.sstables_opened?.toString() || '—'} />
      <Detail label="Query Lo" value={event.query_lo || '—'} />
      <Detail label="Query Hi" value={event.query_hi || '—'} />
      <Detail label="Timestamp" value={new Date(event.timestamp_us / 1000).toLocaleTimeString()} />
    </div>
  );
}

function Detail({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <label className="font-semibold text-slate-700">{label}</label>
      <p className="text-slate-600 truncate">{value}</p>
    </div>
  );
}
