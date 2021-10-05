
import json
import argparse
import os

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('module_template_dir')
    args = parser.parse_args()

    modulePath = os.path.abspath(args.module_template_dir)
    moduleName = os.path.basename(modulePath)

    with open(os.path.join(modulePath, 'analysis', 'default_filters.analysis')) as filtersFile:
        filtersJson = json.load(filtersFile)

    sources = filtersJson['AnalysisNG']['sources']


    for source in sources:
        name = source['name']
        if name.find(moduleName) < 0:
            source['name'] = moduleName + "." + name

    operators = filtersJson['AnalysisNG']['operators']

    for op in operators:
        name = op['name']
        if name.find(moduleName) < 0:
            op['name'] = moduleName + "." + name

    with open(os.path.join(modulePath, 'analysis', 'default_filters.analysis'), 'w') as filtersFile:
        json.dump(filtersJson, filtersFile, indent=4)
