import { useState, type FormEvent } from 'react';
import type { Connection } from './dewRender';

export interface FormValues {
  /** Dataset URL, e.g. s3://bucket/prefix. */
  url: string;
  /** Connection string: the remaining parameters as the CLI ";"-separated grammar. */
  connectionString: string;
}

export const EMPTY_FORM: FormValues = {
  url: '',
  connectionString: '',
};

/** The public demo datasets. All three live in the same anonymous-read bucket, so one connection string
 *  serves them; picking one swaps the dataset without retyping anything. */
export const PUBLIC_CONNECTION = 'region=eu-north-1;anonymous=true';
export const PUBLIC_DATASETS: { label: string; url: string }[] = [
  { label: 'Św. Anny', url: 's3://limilind-public/points/g_sw_anny' },
  { label: 'Kościół Libusza', url: 's3://limilind-public/points/kosciol_libusza' },
  { label: 'Pałac Moszna', url: 's3://limilind-public/points/palac_moszna' },
];

/** Build a Connection from form values, or null if the dataset URL is missing. */
export function buildConnection(v: Partial<FormValues>): Connection | null {
  const url = (v.url ?? '').trim();
  if (!url) return null;
  return { url, connectionString: (v.connectionString ?? '').trim() };
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
      setFormError('A dataset URL is required.');
      return;
    }
    setFormError(null);
    onConnect(connection);
  };

  // Editable WHILE connected: pointing the viewer at another dataset should not mean disconnecting
  // first, retyping the URL and reconnecting. Only an in-flight connect locks the fields.
  const disabled = busy;

  // Switching dataset works whether or not one is loaded: the renderer tears the old data source down
  // and builds the new one, which is the same path Connect takes. Without this, trying another demo
  // meant Disconnect, retype the URL, Connect.
  const pick = (url: string) => {
    const next = { url, connectionString: PUBLIC_CONNECTION };
    setValues(next);
    setFormError(null);
    onConnect({ url, connectionString: PUBLIC_CONNECTION });
  };

  return (
    <form className="connect" onSubmit={onSubmit}>
      <div className="field">
        <span>Public datasets</span>
        <div className="presets">
          {PUBLIC_DATASETS.map((dataset) => (
            <button
              key={dataset.url}
              type="button"
              className={values.url === dataset.url ? 'btn btn--preset btn--preset-active' : 'btn btn--preset'}
              disabled={busy}
              onClick={() => pick(dataset.url)}
            >
              {dataset.label}
            </button>
          ))}
        </div>
      </div>
      <label className="field">
        <span>Dataset URL</span>
        <input
          type="text"
          value={values.url}
          placeholder="s3://bucket/prefix"
          disabled={disabled}
          onChange={(e) => set('url', e.target.value)}
        />
      </label>
      <label className="field">
        <span>Connection string</span>
        <textarea
          className="field__area"
          value={values.connectionString}
          placeholder={'endpoint=http://127.0.0.1:9000;access_key_id=…;secret_access_key=…;region=us-east-1;path_style=true'}
          disabled={disabled}
          rows={5}
          spellCheck={false}
          onChange={(e) => set('connectionString', e.target.value)}
        />
      </label>
      <p className="connect__hint">
        The dataset URL is <code>scheme://bucket/prefix</code>; the connection string is the same{' '}
        <code>key=value;…</code> grammar as the CLI tools. Keys: <code>endpoint</code>,{' '}
        <code>access_key_id</code>, <code>secret_access_key</code>, <code>session_token</code>,{' '}
        <code>region</code>, <code>path_style</code>.
      </p>

      {formError && <p className="connect__error">{formError}</p>}

      {/* Two SEPARATE buttons with distinct keys, never one whose `type` flips. React would otherwise
          reuse the same <button> DOM node across the connected/disconnected swap and merely mutate
          `type` from "button" to "submit" during the click's synchronous flush — so an in-flight
          "Disconnect" click would land on a submit button and re-connect immediately (the "briefly
          Connecting…, then Disconnect again" symptom). */}
      <button key="connect" type="submit" className="btn" disabled={busy}>
        {busy ? 'Connecting…' : connected ? 'Load' : 'Connect'}
      </button>
      {connected && (
        <button key="disconnect" type="button" className="btn btn--ghost" onClick={onDisconnect}>
          Disconnect
        </button>
      )}
    </form>
  );
}
