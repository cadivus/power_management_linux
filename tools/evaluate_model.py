#!/usr/bin/env python

import sys

class EnergyTerm:
    def evaluate(self, frequency):
        pass

class LinearTerm(EnergyTerm):
    def __init__(self, coefficient):
        self.coefficient = coefficient

    def evaluate(self, frequency):
        return self.coefficient * frequency

class SquaredTerm(EnergyTerm):
    def __init__(self, coefficient):
        self.coefficient = coefficient

    def evaluate(self, frequency):
        return self.coefficient * frequency * frequency

class EnergyModel:
    def __init__(self, intercept, terms):
        self.intercept = intercept
        self.terms = terms

    def evaluate(self, frequencies):
        energy = self.intercept
        for term, frequency in zip(self.terms, frequencies):
            energy += term.evaluate(frequency)
        return energy

models = {
    "E": EnergyModel(0, [
        LinearTerm(3.048792273507325e-10),
        LinearTerm(4.7155159305481095e-11),
        SquaredTerm(-1.6217383792175577e-18),
        LinearTerm(-1.8876339894242007e-10),
        LinearTerm(5.001291010517235e-09),
        LinearTerm(2.666471030028046e-10),
    ]),
    "P": EnergyModel(0, [
        LinearTerm(7.886563445880864e-10),
        LinearTerm(3.322996314075699e-10),
        LinearTerm(1.1650503993504607e-09),
        LinearTerm(3.0849775100557654e-11),
        LinearTerm(6.659123282528457e-10),
        LinearTerm(1.0850461620013055e-10),
    ]),
}

def read_proc_file(file):
    data = dict()
    for line in file:
        key, value = line.split(':')
        data[key] = value.strip()
    return data

proc_data = read_proc_file(sys.stdin)

counter_names = ["Counter0", "Counter1", "Counter2", "Counter3", "Counter4", "Counter5"]
counters = (float(proc_data[name]) for name in counter_names)

duration = float(proc_data["Duration"])
if duration == 0:
    duration = 1
duration *= 1e-9;

frequencies = (count / duration for count in counters)

model = models[proc_data["CoreType"]]

power = model.evaluate(frequencies)

print("Reported Power: ", proc_data["Power"])
print("Evaluated Power: ", power)
