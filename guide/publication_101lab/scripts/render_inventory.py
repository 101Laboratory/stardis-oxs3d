"""Render inventory tables from saved evidence; does not modify research sources."""
import json
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
E = BASE / 'evidence'

def read(name): return json.loads((E / name).read_text(encoding='utf-8'))

def main():
    s, repos, remote = read('local-summary.json'), read('local-git.json'), read('remote-github.json')
    uses = {
        'guide': '架构、迁移、物理、并行化和未来路线；随主代码发布',
        'debug_issues': '问题证据与结论；超大 trace 放同版本附件',
        'optimization': '优化与负结果；保留关联实验分支',
        'GPU_WF_Validation': '全 GPU 探索验证的代码和结果',
        'Stardis-Starter-Pack': '示例、场景、模型、实验输出和脚本；需拆清输入/结果',
        'cuda-duplex-validation': 'CUDA 双工诊断源码、trace、Nsight 记录',
        'optix-throughput-validation': '后端基准项目，含需要排除的构建树',
        'perf_diag': '吞吐/性能 CSV 与诊断采样',
        'physical_consistency_stats': 'CPU/GPU 一致性 CSV、绘图程序和图',
        'profiling': '大型 VTune 原始档案；同仓库数据附件',
        'scripts': '实验运行、计时分析或论文工具脚本',
        'stardis-cpu': 'Windows 构建迁移成果，CPU 对照组；导入统一主库',
        'stardis-oxs3d': '核心 GPU 主库与实验分支共同 Git 对象库',
        'Chapters': '论文正文源文件', 'Figures': '论文图源及必要编译输入',
        'Figures - bak': '重复/旧图备份，源工程排除，研究档案按内容去重',
        'Template': '论文模板子模块，带本地修改', 'OurWork': '论文实验与实现笔记，配套主库',
        'ReviewComments': '答辩/评审和响应资料，纳入公开文档', 'report': '审查/模拟评审/修改报告，保留性质标签',
        'References': '文献整理', 'ref_need': '引用需求与文献笔记',
        'src': '编辑器源码', 'tests': '编辑器测试', 'design': '编辑器设计文档',
        'config_examples': '示例参数', 'config_library': '配置库，检查可移植路径',
        '.opencode': '本机开发辅助材料，区分有价值的说明与本机状态',
        '.vscode': '编辑器设置，发布前检查用途与路径',
        '.github': '继承自动化，需核实适用性',
    }
    lines = ['# 本地成果清单', '', '返回 [总目录](README.md)。盘点覆盖三个研究工作区；统计包含构建产物和重复 worktree，不是发布包大小。文件枚举清单见 [local-files.csv](evidence/local-files.csv)。', '',
             '## 工作区和仓库结构', '',
             '`D:/Stardis-GPU` 顶层不是 Git 仓库，包含 Windows 迁移基线、OptiX 核心主库及其 8 个附加工作树。编辑器、论文分别有独立仓库，论文模板是子模块。合计 13 个 Git 位置，不代表 13 个项目；最终 CPU/GPU 及研究资料归到一个 `stardis-oxs3d` 主库。', '',
             '原始 Stardis 属于法国 Meso-Star 团队；本地模块目录中的上游代码和依赖不应计为个人原创。贡献演进见 [归属与结构](09-lineage-and-nested-repos.md)。', '']
    for root in sorted({x['root'] for x in s['groups']}):
        groups = [x for x in s['groups'] if x['root'] == root]
        lines += [f'## {Path(root).name}', '', f"{sum(g['files'] for g in groups):,} 个枚举文件，{sum(g['bytes'] for g in groups)/1024**2:,.2f} MiB。", '', '| 目录 | 文件数 | MiB | 内容及发布落点 |', '|---|---:|---:|---|']
        for g in groups:
            name = g['directory']
            use = uses.get(name, '保留来源和用途，按文件级清单整理')
            if name.startswith('stardis-oxs3d-'): use = '同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布'
            lines.append(f"| `{name}` | {g['files']:,} | {g['bytes']/1024**2:,.2f} | {use} |")
        lines.append('')
    lines += ['## 容易漏掉的材料', '',
              '- `OurWork/` 的性能分解、pool scan、单双池对比和后端解释，需要与主库实验一起整理。',
              '- Starter Pack 的场景目录混有 `.ht`、stderr 日志、渲染图、Python/PowerShell 脚本和编辑器工程，不宜整体当作纯示例文件夹。',
              '- `GPU_WF_Validation/` 与 `guide/GPU_WF_Validation/` 分别有代码/数据和设计说明；两者都要纳入。',
              '- 失败优化、未解决问题和旧 cus3d 代码是研究过程的一部分，用明确状态和版本保留。',
              '- 论文模板局部修改、实验 worktree 未提交源码、stash 不会自动随主分支发布。', '',
              '## 大文件示例', '', '| 工作区 | 文件 | MiB |', '|---|---|---:|']
    for r in s['largest_files'][:10]: lines.append(f"| {Path(r['root']).name} | `{r['path']}` | {r['bytes']/1024**2:,.2f} |")
    lines += ['', '其中 IDE IPCH 是构建缓存，研究 trace 和剖析 session 是原始实验资料；体积相近不代表相同归档价值。完整超过 100 MiB 的列表在 [local-summary.json](evidence/local-summary.json)。', '',
              '## 扫描口径', '', f"合计 **{s['files']:,} 文件，{s['bytes']/1024**3:.3f} GiB**，扫描错误 {len(s['errors'])}。排除 Git 对象、虚拟环境、部分缓存和本发布规划目录；不做内容去重。当前文件清单覆盖旧版本与生成物，发布范围以方案、逐文件审查和用户约束为准。"]
    (BASE/'01-local-inventory.md').write_text('\n'.join(lines)+'\n',encoding='utf8')

    lines = ['# 远端成果与本地版本对应', '', '返回 [总目录](README.md)。远端取自 2026-10-09 公开 GitHub API；完整响应见 [remote-github.json](evidence/remote-github.json)。现有 `stardis-cuda` 仅是历史远端名，目标成果名称是 `stardis-oxs3d`。', '',
             '## 与毕业研究直接相关的公开仓库', '', '| 当前远端 | 已核实情况 | 发布关系 |', '|---|---|---|',
             '| [EricSolshkov/stardis](https://github.com/EricSolshkov/stardis) | `master`，`84c43566c01c`，GPL-3.0 元数据 | 法国团队原始 Stardis 的镜像/派生入口，不是个人原创求解器 |',
             '| [EricSolshkov/stardis-cuda](https://github.com/EricSolshkov/stardis-cuda) | `main`、`opt/merge-phase` 均为 `d89963a3f09d`；无公开 tag/release | 本地 `stardis-oxs3d` 的 remote；整合后更名发布为组织 `stardis-oxs3d` |',
             '| [EricSolshkov/stardis-editor](https://github.com/EricSolshkov/stardis-editor) | `master=89b66b1d7f9a`，与本地 HEAD 一致；无公开 tag/release | 本地有少量功能/测试修改，整理后发布到组织 |',
             '| [EricSolshkov/custar-3d](https://github.com/EricSolshkov/custar-3d) | 仓库存在，分支列表为空 | 不能依赖它恢复放弃的 CUDA 路线；查核心 Git 历史与旧归档 |',
             '| [EricSolshkov/cuBQL](https://github.com/EricSolshkov/cuBQL)、[EricSolshkov/embree](https://github.com/EricSolshkov/embree) | 依赖 fork，公开元数据为 Apache-2.0 | 记录版本与来源，具体个人修改范围待核实 |', '',
             '本地 CPU 的 remote 指向 `EricSolshkov/stardis-win`；论文指向 `EricSolshkov/thesis`。两者公开 API 返回 404，表示本次未能验证远端，不能据此断言不存在、未上传或是私有。CPU 本地工程及 origin 的关系已核实。', '',
             '## 本地 checkout 与当前状态', '', '| 位置 | 分支 | HEAD | 工作区状态 |', '|---|---|---|---|']
    for r in repos:
        status = r['status']['text'].splitlines()
        changes = status[1:]
        note = '干净' if not changes else str(len(changes))+' 条修改/未跟踪/子模块状态'
        if '[ahead 1]' in r['status']['text']: note += '；相对本地 upstream 缓存领先 1'
        lines.append(f"| `{r['path'].replace(chr(92),'/')}` | `{r['branch']['text']}` | `{r['head']['text'][:12]}` | {note} |")
    lines += ['', '公开 API 已独立确认 GPU 当前 main 比本地少 `407766c`（属性冲突诊断提交），编辑器远端 HEAD 与本地一致。论文的领先状态仅对本地缓存成立；远端尚未可读。', '',
              'CPU 与 GPU 树里没有当前 gitlink，但 CPU 早期历史有 30 个嵌套模块引用，其中 29 条引用缺少提交对象（27 个不同 SHA）；详见 [结构检查](09-lineage-and-nested-repos.md)。', '',
              '## 发布前不能漏掉的历史', '',
              '- GPU 主树有 10 个本地分支、9 个 checkout；公开 remote 目前只有 2 个分支。',
              '- `opt/o14-partition` 未合入 `main`。O16 和 stream-auto-order 的提交已在当前主线祖先中，保留名字和负结果/实验说明仍有价值。',
              '- `exp/cpu-wavefront-embree`、WoS 相关工作树和验证工作树有未提交研究代码，不能只推分支引用。',
              '- 两个 stash 分别涉及 GPU_PP 与 PCIe overlap 插桩，原始 SHA 在 supplemental-git.json。',
              '- `.worktree-state` 是本机管理文件，不替代 Git 分支关系或实验版本说明。', '',
              '## 两个账号的公开仓库总清单', '',
              '组织已有仓库均不承担本研究的主项目职责。个人其他仓库列出以说明远端盘点范围，未纳入毕业成果发布批次；GitHub fork 标志不等于原创归属（镜像可能显示 fork=false）。', '']
    relevant={'stardis','stardis-cuda','stardis-editor','stardis-win','thesis','custar-3d','cuBQL','embree'}
    for owner, account in remote['accounts'].items():
        lines += [f'### {owner}', '', f"公开列表 {len(account['repositories'])} 个，分页读取完成：{account['complete']}。", '', '| 仓库 | fork | 默认分支 | 最近 push UTC | 与本次关系 |', '|---|---|---|---|---|']
        for r in account['repositories']:
            role='研究核心或依赖' if r['name'] in relevant else '现有组织成果，保持独立' if owner=='101Laboratory' else '未纳入本研究批次'
            lines.append(f"| [{r['name']}]({r['html_url']}) | {'是' if r['fork'] else '否'} | `{r['default_branch']}` | {r['pushed_at']} | {role} |")
        lines.append('')
    lines += ['## 远端范围限制', '', '已核实的是两个指定账号当前公开内容，未证明私有仓库不存在，也未搜索所有组织成员的个人仓库。上述时间为 GitHub pushed_at UTC 原值；盘点日期采用用户的 Asia/Shanghai 日期。']
    (BASE/'02-remote-and-git.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    print('Rendered 01-local-inventory.md and 02-remote-and-git.md')

if __name__ == '__main__': main()
