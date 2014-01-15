#!/usr/bin/env python
# -*- coding: iso-8859-15 -*-

import sys
import os
import math
import pandas as pd
from decimal import Decimal

# this function checks for the type of parsed string
def typeofvalue(text):
    try:
        int(text)
        return int
    except ValueError:
        pass

    try:
        float(text)
        return float
    except ValueError:
        pass

    return str

soluname = sys.argv[1]
testname = sys.argv[2]
outname = sys.argv[3]

outfile = open(outname,'r')
outlines = outfile.readlines()
outfile.close()

# print identifier
print
print outlines[2]

# maximum length of instance names
namelength = 18

# tolerance for solution value check
tolerance = 1e-6

instances = {}
data = {'status': 'unknown',
        'value': '',
        'rows': '',
        'cols': '',
        'presolrows': '',
        'presolcols': '',
        'iters': '',
        'primaliters': '',
        'dualiters': '',
        'time': '',
        'primalviol': '',
        'dualviol': '',
        'solustat': '',
        'soluval': ''}

for idx, outline in enumerate(outlines):
    if outline.startswith('@01'):
        # convert line to used instance name
        linesplit = outline.split('/')
        linesplit = linesplit[len(linesplit) - 1].rstrip(' \n').rstrip('.gz').rstrip('.GZ').rstrip('.z').rstrip('.Z')
        linesplit = linesplit.split('.')
        instancename = linesplit[0]
        for i in range(1, len(linesplit)-1):
            instancename = instancename + '.' + linesplit[i]
        length = len(instancename)
        if length > namelength:
            instancename = instancename[length-namelength-2:length-2]

        # initialize empty entry
        instances[instancename] = {key:val for key,val in data.iteritems()}

    # invalidate instancename
    elif outline.startswith('=ready='):
        instancename = ''

    elif outline.startswith('SoPlex status'):
        instances[instancename]['status'] = outline.split()[-1].strip('[]')

    elif outline.startswith('Solution'):
        instances[instancename]['value'] = float(outlines[idx+1].split()[-1])

    elif outline.startswith('Original problem'):
        instances[instancename]['cols'] = int(outlines[idx+1].split()[-1])
        instances[instancename]['rows'] = int(outlines[idx+6].split()[-1])

    elif outline.startswith('Iterations'):
        instances[instancename]['iters'] = int(outline.split()[-1])

    elif outline.startswith('  Primal'):
        instances[instancename]['primaliters'] = int(outline.split()[-2])

    elif outline.startswith('  Dual'):
        instances[instancename]['dualiters'] = int(outline.split()[-2])

    elif outline.startswith('Total time'):
        instances[instancename]['time'] = float(outline.split()[-1])

    elif outline.startswith('Violation'):
        primviol = outlines[idx+2].split()[-3]
        dualviol = outlines[idx+3].split()[-3]
        if typeofvalue(primviol) in [int,float] and typeofvalue(dualviol) in [int,float]:
            instances[instancename]['primalviol'] = float(primviol)
            instances[instancename]['dualviol'] = float(dualviol)
        else:
            instances[instancename]['primalviol'] = '-'
            instances[instancename]['dualviol'] = '-'

    elif outline.startswith('Primal solution infeasible') or outline.startswith('Dual solution infeasible'):
        instances[instancename]['status'] = 'fail'
        fail = fail + 1

# parse data from solufile
solufile = open(soluname,'r')

for soluline in solufile:
    solu = soluline.split()
    tag = solu[0]
    name = solu[1]
    if len(solu) == 3:
        value = solu[2]
        if typeofvalue(value) in [int,float]:
            value = float(value)
    else:
        if tag == '=inf=':
            value = 'infeasible'
        else:
            value = 'unknown'
    if name in instances:
        instances[name]['soluval'] = value
        # check solution status
        if value in ['infeasible', 'unbounded']:
            if not instances[name]['status'] == value:
                instances[name]['status'] = 'fail'
        else:
            if (abs(instances[name]['value'] - value))/max(abs(instances[name]['value']),abs(value)) > tolerance:
                instances[name]['status'] = '('+str(value)+') inconsistent'

solufile.close()

df = pd.DataFrame(instances).T

fails = sum(1 for name in instances if instances[name]['status'] == 'fail')
timeouts = sum(1 for name in instances if instances[name]['status'] == 'timeout')
infeasible = sum(1 for name in instances if instances[name]['status'] == 'infeasible')
optimal = sum(1 for name in instances if instances[name]['status'] == 'optimal')

print df[['rows','cols','primalviol','dualviol','iters','time','value','status']].to_string(float_format=lambda x:"%6.9g"%(x))
print
print 'Results: (testset '+testname.split('/')[-1].split('.')[-2]+', settings '+outname.split('/')[-1].split('.')[-2]+'):'
print '{} total, {} optimal, {} fails, {} timeouts, {} infeasible'.format(len(instances),optimal,fails,timeouts,infeasible)

# check for missing files
testfile = open(testname,'r')

for testline in testfile:
    linesplit = testline.split('/')
    linesplit = linesplit[len(linesplit) - 1].rstrip(' \n').rstrip('.gz').rstrip('.GZ').rstrip('.z').rstrip('.Z')
    linesplit = linesplit.split('.')
    instancename = linesplit[0]
    for i in range(1, len(linesplit)-1):
        instancename = instancename + '.' + linesplit[i]
    length = len(instancename)
    if length > namelength:
        instancename = instancename[length-namelength-2:length-2]
    if not instancename in instances:
        print 'mssing instance: '+instancename

testfile.close()
