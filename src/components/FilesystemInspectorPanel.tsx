import React, { useState } from 'react';
import {
  FileCheck2,
  FileWarning,
  FolderPlus,
  RotateCcw,
  Upload,
} from 'lucide-react';
import {
  LauncherState,
  TnsInspectionResult,
  VirtualFileEntry,
  buildTiDocZipHeader,
  buildZehnTnsHeader,
  inspectTnsBuffer,
} from '../engine/nlauncherEngine';

interface FilesystemInspectorPanelProps {
  state: LauncherState;
  onAddVirtualFile: (entry: VirtualFileEntry) => void;
  onResetFilesystem: () => void;
}

export const FilesystemInspectorPanel: React.FC<FilesystemInspectorPanelProps> = ({
  state,
  onAddVirtualFile,
  onResetFilesystem,
}) => {
  const [statusFilter, setStatusFilter] = useState<'ALL' | 'ACCEPTED' | 'REJECTED'>('ALL');
  const [newPath, setNewPath] = useState('/documents/games/arcade/TrackMania-CX.tns');
  const [newAuthor, setNewAuthor] = useState('Homebrew Dev');
  const [newType, setNewType] = useState<'ZEHN_EXEC' | 'TI_DOC' | 'SYS_FILE'>('ZEHN_EXEC');
  const [formError, setFormError] = useState('');
  const [uploadedInspection, setUploadedInspection] = useState<{
    filename: string;
    result: TnsInspectionResult;
  } | null>(null);

  const filteredDiagnostics = state.diagnostics.filter((d) => {
    if (statusFilter === 'ALL') return true;
    if (statusFilter === 'ACCEPTED') return d.status === 'ACCEPTED';
    return d.status !== 'ACCEPTED';
  });

  const handleAddSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const trimmed = newPath.trim();
    if (!trimmed) {
      setFormError('Veuillez saisir un chemin de fichier sur la calculatrice.');
      return;
    }
    if (!trimmed.toLowerCase().endsWith('.tns')) {
      setFormError('Le chemin doit se terminer par l’extension .tns.');
      return;
    }
    setFormError('');

    const filename = trimmed.split('/').pop() || 'Jeu.tns';
    const cleanName = filename.replace(/\.tns$/i, '');

    if (newType === 'ZEHN_EXEC') {
      onAddVirtualFile({
        path: trimmed.startsWith('/') ? trimmed : `/documents/games/${trimmed}`,
        sizeBytes: 94208,
        headerBytes: buildZehnTnsHeader({
          execName: cleanName,
          execAuthor: newAuthor.trim() || 'Homebrew Dev',
          version: 1,
          relocCount: 22,
          allocSize: 262144,
          totalSize: 94208,
          usesLcdBlit: true,
          supportsHww: true,
        }),
      });
    } else if (newType === 'TI_DOC') {
      onAddVirtualFile({
        path: trimmed.startsWith('/') ? trimmed : `/documents/Lycee/${trimmed}`,
        sizeBytes: 21504,
        headerBytes: buildTiDocZipHeader(),
      });
    } else {
      onAddVirtualFile({
        path: `/documents/ndless/${filename}`,
        sizeBytes: 40960,
        headerBytes: buildZehnTnsHeader({
          execName: 'ndless_resources',
          execAuthor: 'Ndless',
          version: 45,
          relocCount: 10,
          allocSize: 90000,
          totalSize: 40960,
          usesLcdBlit: true,
          supportsHww: true,
        }),
      });
    }
  };

  const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const arrayBuffer = await file.arrayBuffer();
    const bytes = new Uint8Array(arrayBuffer);
    const virtualPath = `/documents/uploaded/${file.name}`;
    const res = inspectTnsBuffer(bytes, file.size, virtualPath);
    setUploadedInspection({ filename: file.name, result: res });

    onAddVirtualFile({
      path: virtualPath,
      sizeBytes: file.size,
      headerBytes: bytes.slice(0, 4096),
    });
  };

  return (
    <div className="space-y-6">
      {/* Top KPI Summary Strip */}
      <div className="grid grid-cols-2 sm:grid-cols-5 gap-3 text-xs font-mono">
        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-3.5">
          <div className="text-slate-400">Fichiers .tns scannés</div>
          <div className="mt-1 text-xl font-bold text-slate-100">{state.stats.scannedFiles}</div>
        </div>
        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-3.5">
          <div className="text-slate-400">Exécutables Zehn/PRG</div>
          <div className="mt-1 text-xl font-bold text-emerald-400">{state.stats.acceptedGames}</div>
        </div>
        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-3.5">
          <div className="text-slate-400">Docs TI-Nspire rejetés</div>
          <div className="mt-1 text-xl font-bold text-amber-400">{state.stats.rejectedDocs}</div>
        </div>
        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-3.5">
          <div className="text-slate-400">Système / Self exclus</div>
          <div className="mt-1 text-xl font-bold text-sky-400">{state.stats.rejectedSys}</div>
        </div>
        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-3.5">
          <div className="text-slate-400">Doublons éliminés</div>
          <div className="mt-1 text-xl font-bold text-rose-400">{state.stats.rejectedDups}</div>
        </div>
      </div>

      {/* Interactive File Injection & Real .tns File Uploader */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <form
          onSubmit={handleAddSubmit}
          className="rounded-lg border border-slate-800 bg-slate-900/90 p-5 space-y-4"
        >
          <div className="flex items-center justify-between">
            <h3 className="text-sm font-semibold text-slate-100 flex items-center gap-2">
              <FolderPlus className="h-4 w-4 text-sky-400" />
              Injecter un Fichier <code className="text-sky-400">.tns</code> dans{' '}
              <code>/documents</code>
            </h3>
            <button
              type="button"
              onClick={onResetFilesystem}
              className="inline-flex items-center gap-1 text-xs text-slate-400 hover:text-slate-200 transition-colors whitespace-nowrap shrink-0"
            >
              <RotateCcw className="h-3.5 w-3.5" />
              Réinitialiser l’arborescence
            </button>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <div>
              <label
                htmlFor="virtual-tns-path"
                className="block text-xs font-medium text-slate-300 mb-1"
              >
                Chemin du fichier (.tns)
              </label>
              <input
                id="virtual-tns-path"
                type="text"
                value={newPath}
                onChange={(e) => setNewPath(e.target.value)}
                className="w-full rounded border border-slate-700 bg-slate-950 px-3 py-1.5 text-xs font-mono text-slate-100 focus:border-sky-500 focus:outline-none"
                placeholder="/documents/games/MonJeu.tns"
              />
            </div>
            <div>
              <label
                htmlFor="virtual-tns-type"
                className="block text-xs font-medium text-slate-300 mb-1"
              >
                Signature binaire simulée
              </label>
              <select
                id="virtual-tns-type"
                value={newType}
                onChange={(e) =>
                  setNewType(e.target.value as 'ZEHN_EXEC' | 'TI_DOC' | 'SYS_FILE')
                }
                className="w-full rounded border border-slate-700 bg-slate-950 px-3 py-1.5 text-xs text-slate-100 focus:border-sky-500 focus:outline-none"
              >
                <option value="ZEHN_EXEC">
                  Exécutable Ndless (PRG\0 + Zehn v1 @ 0x1ec)
                </option>
                <option value="TI_DOC">
                  Document TI-Nspire ordinaire (Archive ZIP PK\x03\x04)
                </option>
                <option value="SYS_FILE">
                  Fichier système Ndless (ndless_resources.tns)
                </option>
              </select>
            </div>
          </div>

          <div className="flex flex-col sm:flex-row sm:items-end justify-between gap-3">
            <div className="flex-1">
              <label
                htmlFor="virtual-tns-author"
                className="block text-xs font-medium text-slate-300 mb-1"
              >
                Auteur Zehn (EXECUTABLE_AUTHOR)
              </label>
              <input
                id="virtual-tns-author"
                type="text"
                value={newAuthor}
                onChange={(e) => setNewAuthor(e.target.value)}
                className="w-full rounded border border-slate-700 bg-slate-950 px-3 py-1.5 text-xs font-mono text-slate-100 focus:border-sky-500 focus:outline-none"
              />
            </div>
            <button
              type="submit"
              className="rounded-lg bg-sky-500 px-4 py-2 text-xs font-semibold text-slate-950 hover:bg-sky-400 transition-colors whitespace-nowrap shrink-0"
            >
              Ajouter et Scanner
            </button>
          </div>

          {formError && <p className="text-xs text-rose-400 font-medium">{formError}</p>}
        </form>

        {/* Live local .tns binary file inspector */}
        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5 flex flex-col justify-between">
          <div>
            <h3 className="text-sm font-semibold text-slate-100 flex items-center gap-2">
              <Upload className="h-4 w-4 text-emerald-400" />
              Tester un Fichier <code className="text-emerald-400">.tns</code> Réel depuis votre
              Ordinateur
            </h3>
            <p className="mt-1 text-xs text-slate-400 leading-relaxed">
              Importez n’importe quel fichier <code>.tns</code> (un jeu Ndless, un document de cours
              TI-Nspire ou <code>N-Launcher.tns</code>) pour vérifier son en-tête binaire en temps
              réel.
            </p>

            <label
              htmlFor="real-tns-upload"
              className="mt-3 flex cursor-pointer items-center justify-center gap-2 rounded-lg border border-dashed border-slate-700 bg-slate-950/60 px-4 py-3 text-xs font-medium text-slate-200 hover:border-sky-500 hover:bg-slate-950 transition-colors"
            >
              <Upload className="h-4 w-4 text-sky-400" />
              <span>Sélectionner un fichier .tns à inspecter</span>
              <input
                id="real-tns-upload"
                type="file"
                accept=".tns"
                onChange={handleFileUpload}
                className="sr-only"
              />
            </label>
          </div>

          {uploadedInspection ? (
            <div className="mt-3 rounded border border-slate-800 bg-slate-950 p-3 text-xs font-mono">
              <div className="flex items-center justify-between">
                <span className="font-semibold text-slate-100">
                  {uploadedInspection.filename}
                </span>
                <span
                  className={
                    uploadedInspection.result.isExecutable
                      ? 'text-emerald-400 font-semibold'
                      : 'text-amber-400 font-semibold'
                  }
                >
                  Format : {uploadedInspection.result.format}
                </span>
              </div>
              <div className="mt-1 text-slate-400">
                Magic (4 octets) : <code>{uploadedInspection.result.magicHex}</code> · Taille :{' '}
                {uploadedInspection.result.fileSize} octets
              </div>
              <div className="mt-1 text-slate-300">{uploadedInspection.result.reason}</div>
            </div>
          ) : (
            <div className="mt-3 rounded border border-slate-800/80 bg-slate-950/50 p-3 text-xs text-slate-400">
              Signatures reconnues par <code className="text-slate-200">nl_inspect_tns_file()</code> :{' '}
              <code className="text-emerald-400">50 52 47 00</code> (&quot;PRG\0&quot;),{' '}
              <code className="text-sky-400">5A 65 68 6E</code> (&quot;Zehn&quot;),{' '}
              <code className="text-purple-400">62 46 4C 54</code> (&quot;bFLT&quot;). Rejet des archives{' '}
              <code className="text-amber-400">50 4B 03 04</code> (&quot;PK\x03\x04&quot;).
            </div>
          )}
        </div>
      </div>

      {/* Diagnostic Table of All Scanned .tns Files */}
      <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 mb-4">
          <h3 className="text-sm font-semibold text-slate-100">
            Journal d’Inspection Binaire de l’Arborescence <code className="text-sky-400">/documents</code>
          </h3>
          <div className="flex items-center gap-1 rounded-lg bg-slate-950 p-1 border border-slate-800">
            <button
              type="button"
              onClick={() => setStatusFilter('ALL')}
              className={`px-3 py-1 rounded text-xs font-medium transition-colors whitespace-nowrap shrink-0 ${
                statusFilter === 'ALL'
                  ? 'bg-slate-800 text-slate-100'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              Tous ({state.diagnostics.length})
            </button>
            <button
              type="button"
              onClick={() => setStatusFilter('ACCEPTED')}
              className={`px-3 py-1 rounded text-xs font-medium transition-colors whitespace-nowrap shrink-0 ${
                statusFilter === 'ACCEPTED'
                  ? 'bg-emerald-500/20 text-emerald-300'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              Exécutables acceptés ({state.stats.acceptedGames})
            </button>
            <button
              type="button"
              onClick={() => setStatusFilter('REJECTED')}
              className={`px-3 py-1 rounded text-xs font-medium transition-colors whitespace-nowrap shrink-0 ${
                statusFilter === 'REJECTED'
                  ? 'bg-amber-500/20 text-amber-300'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              Rejetés / Ignorés ({state.diagnostics.length - state.stats.acceptedGames})
            </button>
          </div>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full text-left border-collapse text-xs">
            <thead>
              <tr className="border-b border-slate-800 text-slate-400">
                <th className="py-2.5 px-3 font-medium">Statut</th>
                <th className="py-2.5 px-3 font-medium">Chemin sur la calculatrice</th>
                <th className="py-2.5 px-3 font-medium">Magic (0x00)</th>
                <th className="py-2.5 px-3 font-medium">Format</th>
                <th className="py-2.5 px-3 font-medium text-right">Taille</th>
                <th className="py-2.5 px-3 font-medium">Diagnostic d’analyse binaire</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-800/70">
              {filteredDiagnostics.map((d) => {
                const accepted = d.status === 'ACCEPTED';
                return (
                  <tr key={d.path} className="hover:bg-slate-800/40 transition-colors">
                    <td className="py-2.5 px-3 whitespace-nowrap">
                      {accepted ? (
                        <span className="inline-flex items-center gap-1.5 text-emerald-400 font-medium">
                          <FileCheck2 className="h-3.5 w-3.5" />
                          Jeu retenu
                        </span>
                      ) : (
                        <span className="inline-flex items-center gap-1.5 text-amber-400 font-medium">
                          <FileWarning className="h-3.5 w-3.5" />
                          Filtré
                        </span>
                      )}
                    </td>
                    <td className="py-2.5 px-3 font-mono text-slate-200 whitespace-nowrap">
                      {d.path}
                    </td>
                    <td className="py-2.5 px-3 font-mono text-slate-400 whitespace-nowrap">
                      {d.inspection.magicHex}
                    </td>
                    <td className="py-2.5 px-3 font-mono whitespace-nowrap">
                      <span
                        className={
                          accepted ? 'text-sky-400 font-semibold' : 'text-slate-400'
                        }
                      >
                        {d.inspection.format}
                      </span>
                    </td>
                    <td className="py-2.5 px-3 font-mono text-slate-400 text-right whitespace-nowrap">
                      {(d.sizeBytes / 1024).toFixed(1)} Ko
                    </td>
                    <td className="py-2.5 px-3 text-slate-300">{d.inspection.reason}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
};
