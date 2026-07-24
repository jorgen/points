import { useState, type FormEvent } from 'react';
import type { Connection } from './pointsRender';

export interface FormValues {
  endpoint: string;
  bucket: string;
  prefix: string;
  region: string;
  accessKeyId: string;
  secretAccessKey: string;
  sessionToken: string;
  pathStyle: boolean;
}

export const EMPTY_FORM: FormValues = {
  endpoint: '',
  bucket: '',
  prefix: '',
  region: 'us-east-1',
  accessKeyId: '',
  secretAccessKey: '',
  sessionToken: '',
  pathStyle: false,
};

/** Build a Connection from (possibly partial) form values, or null if required fields are missing. */
export function buildConnection(v: Partial<FormValues>): Connection | null {
  if (!v.bucket || !v.accessKeyId || !v.secretAccessKey) return null;
  const prefix = (v.prefix ?? '').replace(/^\/+/, '');
  const url = `s3://${v.bucket}${prefix ? '/' + prefix : ''}`;
  return {
    url,
    creds: {
      accessKeyId: v.accessKeyId,
      secretAccessKey: v.secretAccessKey,
      sessionToken: v.sessionToken || undefined,
      region: v.region || undefined,
      endpoint: v.endpoint || undefined,
      pathStyle: v.pathStyle ?? false,
    },
  };
}

interface ConnectFormProps {
  initial: Partial<FormValues>;
  connected: boolean;
  busy: boolean;
  onConnect: (connection: Connection) => void;
  onDisconnect: () => void;
}

export function ConnectForm({ initial, connected, busy, onConnect, onDisconnect }: ConnectFormProps) {
  // Drop undefined keys so absent URL params don't overwrite the EMPTY_FORM defaults with "undefined".
  const [values, setValues] = useState<FormValues>(() => {
    const provided = Object.fromEntries(Object.entries(initial).filter(([, v]) => v !== undefined));
    return { ...EMPTY_FORM, ...provided } as FormValues;
  });
  const [formError, setFormError] = useState<string | null>(null);

  const set = <K extends keyof FormValues>(key: K, value: FormValues[K]) =>
    setValues((prev) => ({ ...prev, [key]: value }));

  const onSubmit = (e: FormEvent) => {
    e.preventDefault();
    const connection = buildConnection(values);
    if (!connection) {
      setFormError('Bucket, access key, and secret key are required.');
      return;
    }
    setFormError(null);
    onConnect(connection);
  };

  const text = (key: keyof FormValues, label: string, placeholder = '', type = 'text') => (
    <label className="field">
      <span>{label}</span>
      <input
        type={type}
        value={String(values[key])}
        placeholder={placeholder}
        disabled={connected || busy}
        onChange={(e) => set(key, e.target.value as FormValues[typeof key])}
      />
    </label>
  );

  return (
    <form className="connect" onSubmit={onSubmit}>
      {text('endpoint', 'Endpoint', 'http://127.0.0.1:9000 (blank = AWS)')}
      {text('bucket', 'Bucket', 'my-bucket')}
      {text('prefix', 'Prefix', 'path/to/dataset')}
      {text('region', 'Region', 'us-east-1')}
      {text('accessKeyId', 'Access key ID')}
      {text('secretAccessKey', 'Secret access key', '', 'password')}
      {text('sessionToken', 'Session token', 'optional (STS)')}
      <label className="field field--check">
        <input
          type="checkbox"
          checked={values.pathStyle}
          disabled={connected || busy}
          onChange={(e) => set('pathStyle', e.target.checked)}
        />
        <span>Path-style addressing (minio)</span>
      </label>

      {formError && <p className="connect__error">{formError}</p>}

      {connected ? (
        <button type="button" className="btn btn--ghost" onClick={onDisconnect}>
          Disconnect
        </button>
      ) : (
        <button type="submit" className="btn" disabled={busy}>
          {busy ? 'Connecting…' : 'Connect'}
        </button>
      )}
    </form>
  );
}
